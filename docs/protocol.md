# NeedleDB v2 — TCP Protocol Specification

This document is the authoritative wire-protocol reference for NeedleDB v2.  
Any client that speaks to the C++ engine (Python middleware, custom tools, tests) must follow these rules.

For engine internals (binary layout, IVF, concurrency implementation), see [`engine.md`](./engine.md).  
For a product-level overview, see the root [`README.md`](../README.md).

---

## Transport

| Property | Value |
| :--- | :--- |
| Transport | TCP |
| Default port | `8080` (overridable via `.env` `PORT`) |
| Framing | **Line-delimited text**: every request and every response line ends with `\n` |
| Encoding | UTF-8 for identifiers, metadata, and text payloads |
| Authentication | None |
| TLS | None |

The server accepts multiple concurrent TCP connections (thread-per-client).  
Each connection is independent; commands on one connection do not block accept of new connections.  
A process-wide mutex serializes mutations and searches against the shared in-memory store — see [Concurrency notes](#concurrency-notes).

There is **no** multi-command pipelining contract. Clients should send one command, read the full response, then send the next.

---

## Shared rules (all commands)

1. **Tokens** are separated by a single ASCII space (`0x20`) unless a field is length-prefixed (see `INSERT` text).
2. **IDs, metadata keys/values, and non-length-prefixed fields must not contain** spaces, `=`, or newline (`\n` / `\r`).
3. **Byte limits** (not Python character counts) come from `schema.hpp` and are enforced by both engine and well-behaved clients:

| Limit | Constant | Value |
| :--- | :--- | :--- |
| Embedding dimensions | `DIMENSIONS` | 1024 |
| ID length | `ID_LENGTH` | 32 bytes |
| Metadata key or value | `META_DATA_LENGTH` | 32 bytes |
| Metadata pairs per record | `META_DATA_KP_PAIRS` | 3 |
| Text payload | `TEXT_MAX_LENGTH` | 1200 bytes |
| Max `top_k` | `MAX_K_SIMILAR` | 30 |

4. **Errors** always begin with `ERROR ` and end with `\n`. Treat any such line as failure for that command.
5. **Floats** are sent as decimal text (e.g. `0.0123`). The engine parses them with standard C++ string-to-float conversion. Exactly `DIMENSIONS` floats are required for `INSERT` and `QUERY`.
6. On **INSERT**, the engine **L2-normalizes** the vector. All-zero or non-finite inputs fail normalization and return an error.

---

## Commands

### `INSERT`

Persists one vector, its text payload, and optional metadata.

**Request**

```text
INSERT <id> <text_length> <text> <dims> [key=value ...] f1 f2 ... f{dims}\n
```

| Field | Meaning |
| :--- | :--- |
| `<id>` | Unique string ID (≤ 32 bytes). Duplicate IDs are rejected. |
| `<text_length>` | Decimal byte length of the following text field (1…1200). |
| `<text>` | Exactly `<text_length>` bytes. **May contain spaces.** Read by byte count, not by scanning for spaces. |
| `<dims>` | Must be `1024`. |
| `[key=value ...]` | Optional, up to 3 pairs. Keys/values: no spaces, no `=`, ≤ 32 bytes each. |
| `f1 … f{dims}` | Exactly 1024 decimal floats. |

**Responses**

| Line | Meaning |
| :--- | :--- |
| `INSERT <Successful>\n` | Stored. |
| `INSERT <Successful>, WARNING<OPTIMIZE needed for better searches.>\n` | Stored, but live count crossed the optimize heuristic (see below). |
| `ERROR <...>\n` | Rejected (duplicate id, bad length, normalization failure, parse error, etc.). |

**Optimize warning heuristic** (engine-side): after the insert, if live vector count ≥ `OPTIMIZE_REM_STARTS_AT` (500) **and** live count ≥ `last_build_at × OPTIMIZE_FACTOR` (2), the success line includes the WARNING clause. Clients may ignore it or surface it in a UI.

---

### `QUERY`

Approximate nearest-neighbour search via IVF, with optional metadata pre-filter.

**Request**

```text
QUERY <top_k> <dims> [key=value ...] f1 f2 ... f{dims}\n
```

| Field | Meaning |
| :--- | :--- |
| `<top_k>` | Desired number of hits; clamped to `MAX_K_SIMILAR` (30). |
| `<dims>` | Must be `1024`. |
| `[key=value ...]` | Optional filters. An **empty value** is treated as a wildcard for that key. |
| `f1 … f{dims}` | Query vector (1024 floats). |

**Successful response** (multiple lines)

```text
QUERY <top_k>
<id> <similarity_score> <text>
<id> <similarity_score> <text>
...
END
```

- Header echoes the effective `top_k` (after clamping).
- Zero or more result lines follow. Fewer than `top_k` lines is allowed (sparse data, aggressive filters, or geometric/metadata interaction — see engine limitations).
- `<text>` is the stored payload for that id (may contain spaces); it runs to end-of-line.
- Similarity is a **dot-product** score on L2-normalized vectors (equivalent to cosine similarity).
- Terminator is a line containing only `END`.

**Error**

```text
ERROR <...>\n
```

---

### `DELETE`

Soft-deletes one record by exact string id (tombstone). May trigger auto-compaction.

**Request**

```text
DELETE <id>\n
```

**Responses**

| Line | Meaning |
| :--- | :--- |
| `DELETE <Successful>\n` | Tombstoned. |
| `DELETE <Successful>, Compaction<Successful>\n` | Tombstoned and compaction finished. |
| `DELETE <Successful>, WARNING <Database compaction failed>.\n` | Tombstoned; compaction attempted and failed. |
| `ERROR <...>\n` | e.g. id not found. |

**Auto-compaction trigger:** when `(dead_entries × DELETE_FACTOR) ≥ total_vector_count` with `DELETE_FACTOR = 8`. Compaction rewrites entry + text files and rebuilds RAM state. It holds the global store lock for the duration.

---

### `SAVE`

Flushes the in-memory header (live/total counts and related state) to the entry database file.

**Request**

```text
SAVE\n
```

**Response**

```text
SAVE <Successful>\n
```

or `ERROR <...>\n`.

---

### `LOAD`

Clears RAM state, reloads live entries from disk, rebuilds the IVF index from scratch, and repersists centroids.

**Request**

```text
LOAD\n
```

**Response**

```text
LOAD <Successful>\n
```

or `ERROR <...>\n`.

This can take a long time on large datasets. Clients should use an extended read timeout (the Python client uses 300s for `LOAD` / `OPTIMIZE`).

---

### `OPTIMIZE`

Runs k-means on the current live set, rebuilds inverted lists, and writes centroids + `last_build_at` to `database_index.vdb`.

**Request**

```text
OPTIMIZE\n
```

**Response**

```text
OPTIMIZE <Successful>\n
```

or `ERROR <...>\n`.

Same long-timeout guidance as `LOAD`. While `OPTIMIZE` runs, the store mutex is held — other clients’ commands wait.

---

## Minimal client algorithm

```text
connect(host, port)
for each operation:
    send(command_line)           # must end with \n
    if command == QUERY:
        read lines until a line equals "END"
        # or until a line starts with "ERROR"
    else:
        read one line
        # success prefixes: INSERT / DELETE / SAVE / LOAD / OPTIMIZE
        # failure prefix:   ERROR
disconnect()
```

**QUERY** is the only multi-line success response. Everything else is a single line.

---

## Concurrency notes

- **Multiple TCP clients** may connect at once.
- **One command at a time per connection** is the safe model.
- Server-side, `handle_client` runs on a dedicated thread per connection; `store_mutex_` protects `INSERT` / `QUERY` / `DELETE` / `SAVE` / `LOAD` / `OPTIMIZE` bodies only.
- Slow embedding or slow LLM work on a client does **not** hold the engine lock — only the brief TCP command does.
- Long `OPTIMIZE`, `LOAD`, or compaction will stall other clients’ commands until finished.

---

## What this protocol intentionally is not

- Not HTTP, not gRPC, not JSON-RPC.
- Not authenticated or encrypted.
- Not a full query language (no `COUNT`, `LIST`, `STATS`, or range deletes in v2).
- Not a guarantee of atomic multi-document transactions.

Those constraints keep the engine small and inspectable. Higher-level behaviour (chunk IDs, ledgers, RAG prompts) belongs in clients such as the Python middleware.

---

## Versioning

Binary on-disk schema version is `VERSION = 6` in `schema.hpp`.  
The text protocol is versioned implicitly with the engine release (v2). Changing command shapes or response prefixes is a breaking change and should bump the project major/minor version and this document together.
