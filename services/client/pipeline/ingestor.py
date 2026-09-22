# =============================================================================
# pipeline/ingestor.py
# Document ingestion pipeline — reads files, splits into chunks,
# embeds each chunk, and inserts into vector database.
#
# Supported file types: .txt  .pdf  .docx
# Install extras:       pip install pypdf python-docx
#
# Why chunking?
#   Embedding models work best on short, focused text (100–200 words).
#   A 2000-word document embedded as one vector loses semantic detail.
#   Chunking at 150 words keeps each vector semantically tight and
#   makes retrieval precise — you get the right paragraph, not the whole doc.
#
# Why metadata?
#   Each chunk is labelled with source=<filename> and chunk_id=<index>.
#   This lets the vector DB filter results by document at query time,
#   so the chatbot can answer "only from the AI document" if needed.
#
# Engine/schema constants (ID_LENGTH, META_DATA_LENGTH, TEXT_MAX_LENGTH, ...)
# are never hardcoded here. ingest_file()/ingest_folder() take an explicit
# `schema` SimpleNamespace — the same one passed to Client.connect() — and
# every limit is read from it. This file is not the source of truth for any
# of those numbers.
# =============================================================================

import os
import re
from pipeline.embedder import embed
from schema import PY_SCHEMA


# File extensions supported by this pipeline
SUPPORTED = PY_SCHEMA.SUPPORTED_EXTENSIONS


# =============================================================================
# TEXT EXTRACTION  — one private reader per file type
# =============================================================================

def _read_txt(filepath: str) -> str:
    """Reads a plain text file and returns its contents as a string."""
    with open(filepath, "r", encoding="utf-8") as f:
        return f.read()


def _read_pdf(filepath: str) -> str:
    """
    Extracts text from all pages of a PDF using pypdf.
    Pages that contain only images (no text layer) are skipped silently.
    Install: pip install pypdf
    """
    from pypdf import PdfReader
    reader = PdfReader(filepath)
    pages  = []
    for page in reader.pages:
        text = page.extract_text()
        if text:              # skip image-only pages that return None or ""
            pages.append(text)
    return "\n".join(pages)


def _read_docx(filepath: str) -> str:
    """
    Extracts text from all non-empty paragraphs of a Word document.
    Install: pip install python-docx
    """
    import docx
    doc        = docx.Document(filepath)
    paragraphs = [p.text for p in doc.paragraphs if p.text.strip()]
    return "\n".join(paragraphs)


def read_file(filepath: str) -> str:
    """
    Public router — reads any supported file and returns plain text.

    Args:
        filepath: path to a .txt, .pdf, or .docx file

    Returns:
        plain text string

    Raises:
        FileNotFoundError: if the path does not exist
        ValueError:        if the file extension is not supported
    """
    if not os.path.exists(filepath):
        raise FileNotFoundError(f"File not found: '{filepath}'")

    ext = os.path.splitext(filepath)[1].lower()

    if ext == ".txt":
        return _read_txt(filepath)
    elif ext == ".pdf":
        return _read_pdf(filepath)
    elif ext == ".docx":
        return _read_docx(filepath)
    else:
        raise ValueError(
            f"Unsupported file type '{ext}'. "
            f"Supported: {', '.join(sorted(SUPPORTED))}"
        )


# =============================================================================
# CHUNKING
# =============================================================================

def chunk_text(text: str, chunk_size: int = PY_SCHEMA.DEFAULT_CHUNK_SIZE) -> list[str]:
    """
    Splits plain text into fixed-size, non-overlapping word-count chunks.

    150 words per chunk is the project default — chosen because:
      - It fits comfortably in the GPT/LLM prompt as context
      - It keeps each embedding semantically focused
      - It keeps metadata values (chunk_id) short (single or double digit)

    Note: text.split()/" ".join() also collapses any newlines/tabs in the
    source document into single spaces, so the resulting chunks are always
    single-line — safe for the engine's line-delimited wire protocol.

    Args:
        text:       plain text to split
        chunk_size: number of words per chunk (default 150)

    Returns:
        list of text chunks (last chunk may be shorter than chunk_size)
    """
    words  = text.split()
    chunks = []
    for i in range(0, len(words), chunk_size):
        chunk = " ".join(words[i : i + chunk_size])
        chunks.append(chunk)
    return chunks


# =============================================================================
# SCHEMA-AWARE HELPERS
# =============================================================================

def _sanitize_for_wire(value: str) -> str:
    """
    Replaces characters the wire protocol can't carry in an id or metadata
    token (whitespace, '=') with '_'. Real-world filenames routinely contain
    spaces ("Q3 Report.pdf"), which would otherwise make every chunk of that
    file fail Client's id/metadata validation.
    """
    return re.sub(r"[\s=]+", "_", value)


def _truncate_utf8(value: str, max_bytes: int) -> str:
    """
    Truncates a string to at most max_bytes UTF-8 bytes without splitting a
    multi-byte character in half. All engine length limits (ID_LENGTH,
    META_DATA_LENGTH, ...) are byte limits, not Python character counts.
    """
    if max_bytes <= 0:
        return ""
    encoded = value.encode("utf-8")
    if len(encoded) <= max_bytes:
        return value
    return encoded[:max_bytes].decode("utf-8", errors="ignore")


def _build_doc_id(base_name: str, chunk_index: int, schema) -> str:
    """
    Builds a unique, protocol-valid doc_id for one chunk:
        <truncated source name>_chunk_<index>

    Fits within schema.ID_LENGTH bytes by construction. The "_chunk_<index>"
    suffix is always kept in full — truncation only ever eats into the
    source-name prefix — so ids stay unique across a document's chunks even
    when the filename itself is long.
    """
    suffix = f"_chunk_{chunk_index}"
    suffix_bytes = len(suffix.encode("utf-8"))
    max_prefix_bytes = max(schema.ID_LENGTH - suffix_bytes, 0)
    prefix = _truncate_utf8(base_name, max_prefix_bytes)
    doc_id = f"{prefix}{suffix}"

    # Defensive fallback for a pathologically small ID_LENGTH where even the
    # suffix alone wouldn't fit — uniqueness can't be guaranteed here, but
    # the byte limit is still respected.
    if len(doc_id.encode("utf-8")) > schema.ID_LENGTH:
        doc_id = _truncate_utf8(doc_id, schema.ID_LENGTH)
    return doc_id


# =============================================================================
# INGESTION
# =============================================================================

def ingest_file(client, filepath: str, schema, chunk_size: int = PY_SCHEMA.DEFAULT_CHUNK_SIZE) -> list[str]:
    """
    Full ingestion pipeline for a single file:
        read → chunk → embed → insert into DB

    Metadata attached to every chunk:
        source   = filename without extension, sanitized for the wire
                   protocol and truncated to schema.META_DATA_LENGTH bytes
        chunk_id = chunk index as a string e.g. "0", "1", "2"
        (protocol allows up to schema.META_DATA_KP_PAIRS pairs; we use 2)

    Args:
        client:     connected Client instance
        filepath:   path to a .txt, .pdf, or .docx file
        schema:     SimpleNamespace of engine schema constants (ID_LENGTH,
                    META_DATA_LENGTH, TEXT_MAX_LENGTH, ...) — the same
                    object passed to Client.connect(). Required; this
                    module makes no assumptions about these limits.
        chunk_size: words per chunk (default 150)

    Returns:
        list of inserted doc_id strings. A chunk that's oversized, fails
        to embed, or fails to insert is skipped with a printed message
        rather than aborting the whole file — the returned list reflects
        exactly what was actually inserted.
    """
    filename  = os.path.basename(filepath)
    ext       = os.path.splitext(filename)[1].lower()
    base_name = _sanitize_for_wire(os.path.splitext(filename)[0])
    source_for_metadata = _truncate_utf8(base_name, schema.META_DATA_LENGTH)

    print(f"\n[ingestor] '{filename}' (type: {ext})")

    # ── Step 1: extract text ──────────────────────────────────────────
    try:
        text = read_file(filepath)
    except ValueError as e:
        print(f"  SKIP — {e}")
        return []
    except Exception as e:
        print(f"  ERROR reading file — {e}")
        return []

    if not text.strip():
        print("  SKIP — file produced no extractable text (image-only PDF?)")
        return []

    # ── Step 2: chunk ─────────────────────────────────────────────────
    chunks = chunk_text(text, chunk_size)
    print(f"  {len(chunks)} chunk(s) of ~{chunk_size} words")

    # ── Step 3: embed + insert ────────────────────────────────────────
    inserted_ids = []

    for i, chunk in enumerate(chunks):
        chunk_bytes = len(chunk.encode("utf-8"))
        if chunk_bytes > schema.TEXT_MAX_LENGTH:
            print(
                f"  [{i+1}/{len(chunks)}] SKIP — chunk is {chunk_bytes} bytes, "
                f"exceeds engine limit of {schema.TEXT_MAX_LENGTH} bytes "
                f"(try a smaller chunk_size)."
            )
            continue

        doc_id = _build_doc_id(base_name, i, schema)
        metadata = {
            "source":   source_for_metadata,   # e.g. "kb_ai_ml"
            "chunk_id": str(i),                # e.g. "0"
        }

        print(f"  [{i+1}/{len(chunks)}] Embedding '{doc_id}'...")
        try:
            vector = embed(chunk)
        except Exception as e:
            print(f"  [{i+1}/{len(chunks)}] ERROR embedding chunk — {e}")
            continue

        try:
            response = client.insert(doc_id, chunk, vector, metadata=metadata)
        except Exception as e:
            print(f"  [{i+1}/{len(chunks)}] ERROR inserting chunk — {e}")
            continue

        print(f"  [{i+1}/{len(chunks)}] Server → {response.strip()}")
        inserted_ids.append(doc_id)

    return inserted_ids


def ingest_folder(client, folder_path: str, schema, chunk_size: int = PY_SCHEMA.DEFAULT_CHUNK_SIZE) -> dict:
    """
    Ingests all supported files (.txt, .pdf, .docx) found in a folder.
    Unsupported files are skipped with a warning.

    Args:
        client:      connected Client instance
        folder_path: path to a folder containing documents
        schema:      SimpleNamespace of engine schema constants, passed
                     straight through to ingest_file() for every file —
                     see ingest_file()'s docstring.
        chunk_size:  words per chunk (default 150)

    Returns:
        dict { filename: [list of inserted doc_ids] }. A file that fails
        entirely — including an error ingest_file() itself didn't catch —
        maps to an empty list rather than aborting the whole run.
    """
    if not os.path.exists(folder_path):
        print(f"[ingestor] ERROR: folder '{folder_path}' not found.")
        return {}

    all_files       = os.listdir(folder_path)
    supported_files = [
        f for f in all_files
        if os.path.splitext(f)[1].lower() in SUPPORTED
    ]
    skipped_files = [
        f for f in all_files
        if f not in supported_files and not f.startswith(".")
    ]

    if not supported_files:
        print(f"[ingestor] No supported files in '{folder_path}'.")
        print(f"           Supported types: {', '.join(sorted(SUPPORTED))}")
        return {}

    print(f"[ingestor] Found {len(supported_files)} supported file(s)")
    if skipped_files:
        print(f"[ingestor] Skipping {len(skipped_files)} unsupported: {skipped_files}")

    all_ids = {}
    for filename in supported_files:
        filepath = os.path.join(folder_path, filename)
        try:
            all_ids[filename] = ingest_file(client, filepath, schema, chunk_size)
        except Exception as e:
            print(f"[ingestor] ERROR processing '{filename}' — {e}")
            all_ids[filename] = []

    total = sum(len(v) for v in all_ids.values())
    print(f"\n[ingestor] Done — {total} chunk(s) from {len(supported_files)} file(s).")
    return all_ids
