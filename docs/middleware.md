# NeedleDB v2 — Python Middleware

This document describes the **Python middleware**: the process that sits between the Streamlit UI (or any other front end) and the C++ engine. It owns embedding, chunking, ingestion bookkeeping, TCP client logic, and the RAG chat loop.

It does **not** implement the vector database. All durable vectors, text payloads, and IVF state live in the C++ engine and are reached only over the TCP protocol documented in [`protocol.md`](./protocol.md).

| If you need… | Read… |
| :--- | :--- |
| Binary layout, IVF, compaction | [`engine.md`](./engine.md) |
| Exact TCP command grammar | [`protocol.md`](./protocol.md) |
| Latency numbers and CPU cost | [`performance.md`](./performance.md) |
| How to run the stack | Root [`README.md`](../README.md) |

---

## Role in the architecture

```text
[ Streamlit UI ]  →  [ Python middleware ]  →  TCP :8080  →  [ C++ engine ]
                              ↓
                         [ Ollama ]
                    embed + generate
```

Responsibilities of this layer:

- Call Ollama for **embeddings** (`mxbai-embed-large`, 1024-d) and **generation** (`qwen2.5:3b`).
- Split documents into chunks, build protocol-valid ids/metadata, and issue `INSERT`s.
- Issue `QUERY` / `DELETE` / `SAVE` / `LOAD` / `OPTIMIZE` through a shared TCP client.
- Keep a **client-side ingestion ledger** (JSON) so the UI can show document lists after restart.
- Assemble RAG prompts from retrieved chunks + short chat history.

What it deliberately does **not** do:

- Open or parse `.vdb` files.
- Own the source of truth for “how many vectors exist in the engine.”
- Provide authentication or multi-tenant isolation.

---

## Layout (`services/client/`)

```text
services/client/
├── app_gui.py              # Streamlit presentation (UI only)
├── app/
│   └── rag_chatbot.py      # RAG orchestration, connect/disconnect, KB helpers
├── pipeline/
│   ├── embedder.py         # Ollama embedding calls
│   ├── ingestor.py         # Read → chunk → embed → INSERT + ledger updates
│   ├── searcher.py         # Thin search helper / CLI-style exploration
│   └── ledger.py           # ingestion_ledger.json read/write
├── vecdb_client.py         # TCP client (framing, validation, timeouts)
├── schema.py               # Python-only defaults (PY_SCHEMA)
├── schema_loader.py        # Parses schema.hpp → engine limits at runtime
├── requirements.txt
└── data/
    ├── knowledge_base/     # Optional seed / uploaded docs (runtime)
    └── ingestion_ledger.json
```

Docker runs Streamlit with `WORKDIR=/app` and `PYTHONPATH=/app/services/client` so imports such as `from pipeline.ledger import …` and `from schema import PY_SCHEMA` resolve.

---

## Configuration

### Engine limits (Single Source of Truth)

Structural constants (`DIMENSIONS`, `ID_LENGTH`, `TEXT_MAX_LENGTH`, `MAX_K_SIMILAR`, `OPTIMIZE_*`, …) are **not** hard-coded as the authority in Python.

On `RAGChatbot.connect()`:

1. `schema_loader.load_cpp_schema()` reads `services/engine/include/schema.hpp`.
2. Regex extraction builds a `SimpleNamespace` of `constexpr` values.
3. That object is passed into `Client.connect(…)` and used for every length/id/vector check before bytes hit the wire.

If the header and the running engine disagree, behaviour is undefined; the intended workflow is one repo, one built engine, one parsed header.

### Python-only defaults (`schema.py` → `PY_SCHEMA`)

| Key | Role |
| :--- | :--- |
| `SOCKET_TIMEOUT_SECONDS` | Default TCP read timeout for ordinary commands. |
| `LONG_OP_TIMEOUT_SECONDS` | Timeout for `LOAD` / `OPTIMIZE` (minutes-scale). |
| `RECV_BUFFER_BYTES` | `socket.recv` chunk size. |
| `DEFAULT_TOP_K` | RAG / search default result count. |
| `DEFAULT_CHUNK_SIZE` | Words per chunk at ingest (default 150). |
| `DEFAULT_MIN_SCORE` | Minimum similarity to keep a chunk in the RAG context. |
| `DEFAULT_MAX_HISTORY_TURNS` | Chat turns retained for the LLM. |
| `SUPPORTED_EXTENSIONS` | `.txt`, `.pdf`, `.docx`. |
| `KB_FOLDER` | Default folder for “Load KB from folder”. |

### Environment (`.env`)

The middleware cares about:

| Variable | Typical Docker value | Purpose |
| :--- | :--- | :--- |
| `IP` | `cpp_server` | Engine hostname. |
| `PORT` | `8080` | Engine TCP port. |
| `OLLAMA_HOST` | `http://ollama:11434` | Ollama HTTP base URL. |
| `EMBEDDING_MODEL` | `mxbai-embed-large` | Embedding model name. |
| `LOCAL_LLM_MODEL` | `qwen2.5:3b` | Chat model name. |

Path keys used by the **engine** (`VECDB_*_DATA_PATH`) are not interpreted by Python for file I/O; Python never opens those paths.

---

## TCP client (`vecdb_client.py`)

`Client` is the only module that formats protocol lines and reads responses.

- Validates id / metadata / text / vector length using the engine schema object.
- Sends one command per call; reads one line for most commands, or until `END` for `QUERY`.
- Raises a dedicated error type when the server replies with `ERROR …`.
- Uses a short timeout for normal ops and `LONG_OP_TIMEOUT_SECONDS` for `LOAD` / `OPTIMIZE`.

Public methods map 1:1 to the protocol: `insert`, `query`, `delete`, `save`, `load`, `optimize`.

The client is **not** internally synchronized for multi-threaded use on one socket. The Streamlit app uses one bot/client per session and runs script steps sequentially.

---

## Ingestion pipeline

### Chunking

- Input: plain text from `.txt`, or extracted text from `.pdf` / `.docx`.
- Split on **word count** (`DEFAULT_CHUNK_SIZE`, default 150 words), non-overlapping.
- Each chunk must encode to ≤ `TEXT_MAX_LENGTH` (1200) **bytes**. Oversized chunks are skipped with a log line, not hard-failed for the whole file.

150 words and 1200 bytes are related only by operational choice: the byte cap must be large enough for typical 150-word UTF-8 chunks (including non-ASCII). They are not derived from one formula in code.

### Identifiers and metadata

For file `Report Q3.pdf`, chunk index `i`:

| Field | Example construction |
| :--- | :--- |
| Doc id | `Report_Q3_chunk_0` (sanitized base name + `_chunk_` + index, truncated to `ID_LENGTH` bytes) |
| Metadata `source` | Sanitized base name, truncated to `META_DATA_LENGTH` |
| Metadata `chunk_id` | `"0"`, `"1"`, … |

Sanitization replaces runs of whitespace and `=` with `_` so tokens remain protocol-safe.

### Ledger (`pipeline/ledger.py`)

Path: `services/client/data/ingestion_ledger.json` (parent of `KB_FOLDER`).

The ledger records, per filename:

- `status`: `successful` | `partial` | `failed`
- `total_chunks` / `accepted_chunks`
- `last_ingested` (UTC ISO timestamp)
- optional `error`

Aggregate `stats.kb_chunks_loaded` is the sum of accepted chunks.

**Why it exists:** the v2 engine has no `LIST` / `COUNT` / `STATS` command. After a process restart, the UI would otherwise show zero documents even when the `.vdb` files still hold data. The ledger is a **client convenience**, not a replica of engine truth. Another client writing to the same engine will not update this file.

Duplicate guard: a filename already marked `successful` or `partial` is refused on re-ingest until the ledger entry is cleared or marked failed (for example after a UI “delete document” flow).

---

## Query and RAG path

1. User question → embed via Ollama → `QUERY top_k` over TCP.
2. Engine returns `(id, score, text)` lines (text is inlined in v2; no local chunk store).
3. Chunks below `DEFAULT_MIN_SCORE` may be dropped before prompt build.
4. System prompt + retrieved text + trimmed history → Ollama chat model.
5. Answer and source ids returned to the UI.

Follow-up questions that depend on prior turns can still fail the similarity gate if the new standalone question does not retrieve the same chunks. Rephrasing with explicit context is a practical workaround; a deeper conversational retrieval policy is left for a later revision.

---

## Streamlit UI (`app_gui.py`)

Presentation only; business logic stays in `RAGChatbot` / pipeline / client.

| Area | Behaviour |
| :--- | :--- |
| Sidebar | Connect/disconnect, load KB folder, live ledger stats, OPTIMIZE threshold hint, engine admin (`OPTIMIZE` / `SAVE` / `LOAD`, delete-by-id). |
| Chat | History, clear control near the input, RAG answers with source captions. |
| Search Explorer | Ad-hoc semantic search, optional metadata filter, per-hit delete. |
| Knowledge Base | Upload/ingest first; then ledger table with per-document delete (reconstructs `*_chunk_*` ids and issues `DELETE`s). |

The OPTIMIZE threshold display uses ledger count plus `OPTIMIZE_REM_STARTS_AT` / `OPTIMIZE_FACTOR` from the loaded schema. It approximates engine warnings; it does not read `last_build_at` from disk (that value is not exposed on the wire in v2).

---

## Timeouts and long operations

| Operation | Typical client timeout | Notes |
| :--- | :--- | :--- |
| `INSERT` / `QUERY` / `DELETE` / `SAVE` | ~15s | Dominated by network + engine critical section; embedding is separate and outside the TCP call. |
| `LOAD` / `OPTIMIZE` | ~300s | Full index rebuild possible; UI should show a spinner and expect blocking of other engine commands while the mutex is held. |

Ingestion of hundreds of chunks is slow mainly because of **per-chunk embedding**, not because each `INSERT` holds the engine for minutes. Between inserts the engine can serve other TCP clients (see engine concurrency).

---

## Known middleware limitations

| Topic | Detail |
| :--- | :--- |
| Ledger ≠ engine | Counts and document lists can drift if another client mutates the DB or files are deleted only on one side. |
| No engine STATS | Cannot show true live/tombstone counts without a new protocol command. |
| One socket usage model | Designed for sequential use inside a Streamlit session, not a thread pool sharing one `Client`. |
| Delete-by-document | Reconstructs ids from the same rules as ingest; partial past failures or external inserts with different id schemes will not match. |
| CPU latency | End-to-end chat time is usually dominated by Ollama on CPU; see [`performance.md`](./performance.md). |
| Secrets / multi-user | No authn between UI and middleware, or middleware and engine. |

---

## Related source map

| Concern | Module |
| :--- | :--- |
| TCP speak/listen | `vecdb_client.py` |
| Embeddings | `pipeline/embedder.py` |
| Files → chunks → INSERT | `pipeline/ingestor.py` |
| Ledger I/O | `pipeline/ledger.py` |
| RAG loop | `app/rag_chatbot.py` |
| UI | `app_gui.py` |
| Engine constants | `schema_loader.py` + `services/engine/include/schema.hpp` |
| Python defaults | `schema.py` |
