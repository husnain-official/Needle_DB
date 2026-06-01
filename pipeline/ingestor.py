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
# =============================================================================

import os
from pipeline.embedder import embed

# File extensions supported by this pipeline
SUPPORTED = {".txt", ".pdf", ".docx"}


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

def chunk_text(text: str, chunk_size: int = 150) -> list[str]:
    """
    Splits plain text into overlapping word-count chunks.

    150 words per chunk is the project default — chosen because:
      - It fits comfortably in the GPT prompt as context
      - It keeps each embedding semantically focused
      - It keeps metadata values (chunk_id) short (single or double digit)

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
# INGESTION
# =============================================================================

def ingest_file(client, filepath: str, chunk_size: int = 150) -> list[str]:
    """
    Full ingestion pipeline for a single file:
        read → chunk → embed → insert into DB

    Metadata attached to every chunk:
        source   = filename without extension, trimmed to 32 chars
        chunk_id = chunk index as a string e.g. "0", "1", "2"
        (protocol allows max 3 metadata pairs; we use 2)

    Args:
        client:     connected Client instance
        filepath:   path to a .txt, .pdf, or .docx file
        chunk_size: words per chunk (default 150)

    Returns:
        list of inserted doc_id strings (empty list if ingestion failed)
    """
    filename    = os.path.basename(filepath)
    ext         = os.path.splitext(filename)[1].lower()
    source_name = os.path.splitext(filename)[0][:32]   # protocol: max 32 chars

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
        # Build doc_id — must be max 32 chars with no spaces
        doc_id = f"{source_name[:20]}_chunk_{i}"
        doc_id = doc_id[:32]

        metadata = {
            "source":   source_name,   # e.g. "kb_ai_ml"
            "chunk_id": str(i),        # e.g. "0"
        }

        print(f"  [{i+1}/{len(chunks)}] Embedding '{doc_id}'...")
        vector   = embed(chunk)
        response = client.insert(doc_id, vector, metadata=metadata)
        print(f"  [{i+1}/{len(chunks)}] Server → {response.strip()}")

        inserted_ids.append(doc_id)

    return inserted_ids


def ingest_folder(client, folder_path: str, chunk_size: int = 150) -> dict:
    """
    Ingests all supported files (.txt, .pdf, .docx) found in a folder.
    Unsupported files are skipped with a warning.

    Args:
        client:      connected Client instance
        folder_path: path to a folder containing documents
        chunk_size:  words per chunk (default 150)

    Returns:
        dict { filename: [list of inserted doc_ids] }
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
        filepath        = os.path.join(folder_path, filename)
        all_ids[filename] = ingest_file(client, filepath, chunk_size)

    total = sum(len(v) for v in all_ids.values())
    print(f"\n[ingestor] Done — {total} chunk(s) from {len(supported_files)} file(s).")
    return all_ids
