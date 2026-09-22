# =============================================================================
# schema.py
# Single source of truth for pipeline-level Python constants.
#
# This mirrors the C++ engine's schema.hpp, but for values that belong to
# THIS Python codebase rather than the wire protocol — socket timeouts,
# buffer sizes, default chunk/result sizes, supported file types, etc.
#
# None of these describe the engine's binary format or TCP protocol limits
# (DIMENSIONS, ID_LENGTH, META_DATA_LENGTH, TEXT_MAX_LENGTH, MAX_K_SIMILAR,
# ...). Those stay exactly as they are today: produced by schema_loader.py
# and passed around explicitly as a `schema` object (e.g. to
# Client.connect(), ingest_file(), ingest_folder()). This file never
# defines or guesses at any of those engine values.
#
# Every Python file that needs one of ITS OWN constants imports PY_SCHEMA
# from here instead of hardcoding it locally — this is the only place any
# of these values should be defined. Imported as PY_SCHEMA (not `schema`)
# specifically so it never collides with the ubiquitous local `schema`
# parameter/attribute that already means "the engine schema object"
# throughout this codebase.
# =============================================================================

from types import SimpleNamespace

PY_SCHEMA = SimpleNamespace(
    # ── client/vecdb_client.py — socket behavior ──────────────────────
    SOCKET_TIMEOUT_SECONDS=15,       # INSERT/QUERY/DELETE/SAVE — fine for small payloads
    LONG_OP_TIMEOUT_SECONDS=300,     # LOAD/OPTIMIZE can take minutes on large DBs
    RECV_BUFFER_BYTES=8192,          # per socket.recv() call

    # ── pipeline/searcher.py — search defaults ────────────────────────
    DEFAULT_TOP_K=5,
    TEXT_PREVIEW_CHARS=80,           # print_results() truncates stored text to this

    # ── pipeline/ingestor.py — ingestion defaults ─────────────────────
    DEFAULT_CHUNK_SIZE=150,          # words per chunk
    SUPPORTED_EXTENSIONS={".txt", ".pdf", ".docx"},

    # ── app/rag_chatbot.py — RAG chatbot defaults ─────────────────────
    DEFAULT_MIN_SCORE=0.55,          # min cosine similarity to keep a retrieved chunk
    DEFAULT_MAX_HISTORY_TURNS=6,     # conversation turns kept (1 turn = user + assistant)
)
