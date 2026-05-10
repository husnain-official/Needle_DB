# VectorDB — In-Memory Vector Database (C++ Engine)

> A from-scratch vector database inspired by Pinecone, built in C++ with binary persistence, metadata filtering, TCP networking, and an IVF approximate nearest-neighbour index.

---

## Project Overview

This is the **Person A (Systems / C++) half** of a 6-week, 2-person project to build a mini vector database from the ground up — the same infrastructure that powers semantic search in modern AI applications (RAG pipelines, ChatGPT plugins, Notion AI, etc.).

The C++ engine handles everything from raw float storage to network I/O. A separate Python AI layer (Person B) connects to this server over TCP to embed text and run a RAG chatbot — that half is not included in this repository.

---

## What Has Been Implemented

The following components are fully implemented as of the current state of the codebase:

### — Foundation

- `Vector` struct with `id` (string, max 32 chars), `dims` (int), `data` (`std::vector<float>`), and up to 3 `Metadata_entry` key-value pairs
- `VectorStore` class: in-memory flat array of float embeddings with string ID tracking
- Cosine similarity (two overloads: `vector–vector` and `vector–raw pointer`) implemented with `long double` precision
- Dot-product similarity via `std::inner_product`
- Vector normalisation (`normalise_vector`)
- Brute-force search: `brute_force_search(query, top_k)` returning ranked `(id, score)` pairs
- Shared test vector file (`tests/test_data/test_vectors.json`) — 1536-dimensional vectors in `{ id, dims, data: [float, ...] }` format

### — Storage & Network Protocol

- Binary `.vdb` file format with a fixed-layout `Header` struct (magic number `VDB\0`, version, dimensions, id length, kv length, max key-value pairs, live count, total count, padding)
- `File_manager` class: fixed-size record I/O, `write_vector`, `read_vector`, `delete_vector` (tombstone / soft-delete), `find_by_id` (linear scan), `compact` (rewrite without deleted records), and `flush_header`
- `Vector_Server` class: TCP socket server on port `8080` using `getaddrinfo` / `bind` / `listen` / `accept` (single-threaded; one client at a time)
- **Auto-load on startup**: server reads the `.vdb` file and populates RAM on first `run()`
- `Command_parser`: text-protocol parsing for `INSERT`, `QUERY`, `DELETE`, and `SAVE`

#### TCP Text Protocol

```
INSERT <id> <dims> [key=value ...] f1 f2 f3 ... fN
QUERY  <top_k> [key=value ...] f1 f2 f3 ... fN
DELETE <id>
SAVE
```

Response format: one result per line — `<id> <score>`

### — Metadata & Filtered Search

- `Metadata_entry` array (up to 3 key-value pairs per vector, each field max 32 chars) stored inline in both RAM and on disk
- `get_matching_indices(filter, matches)`: filters vectors by metadata before scoring
- `INSERT` command extended to accept `key=value` pairs before the float block
- `QUERY` command extended to accept optional `key=value` filter

### — IVF Approximate Nearest-Neighbour Index

- Abstract base class `Vector_index` with `build_`, `search_`, `add_`, and `delete_` interface
- `IVF_index` (Inverted File Index) concrete implementation:
  - K-means clustering to build `nlist` centroids over the stored vectors
  - Flattened 1D centroid array for cache-friendly distance computation
  - Per-centroid adjacency lists mapping centroid → vector indices
  - `nprobe`-controlled search: queries only the `nprobe` closest clusters at runtime
  - Squared L2 / Euclidean distance (two overloads: pointer and `std::vector`)
  - Soft-delete support (`delete_` removes an index from its cluster list)

---

## Project Structure

```
.
├── include/
│   ├── types.h            # Core structs: Vector, Metadata_entry, Parse_result, Query_result
│   ├── similarities.hpp   # Inline cosine & dot-product similarity functions
│   ├── vector_store.h     # In-memory store: insert, search, metadata, normalisation
│   ├── file_manager.h     # Binary .vdb persistence: header + fixed-size record I/O
│   ├── vector_server.h    # TCP server: setup, run, stop, client handler
│   ├── command_parser.h   # Protocol parsing: INSERT / QUERY / DELETE / SAVE
│   └── ivf.h              # IVF approximate nearest-neighbour index
│
├── src/
│   ├── main.cpp           # Entry point: wires store, file manager, and server together
│   ├── vector_store.cpp   # VectorStore implementation
│   ├── file_manager.cpp   # File_manager implementation
│   ├── vector_server.cpp  # Vector_Server implementation (TCP + command dispatch)
│   ├── command_parser.cpp # Protocol parser implementation
│   └── ivf.cpp            # IVF_index implementation (k-means build + search)
│
└── tests/
    ├── test_vector_store.cpp   # ⚠️ AI-generated (see note below)
    ├── test_file_manager.cpp   # ⚠️ AI-generated (see note below)
    ├── test_vector_server.cpp  # ⚠️ AI-generated (see note below)
    └── test_data/
        └── test_vectors.json   # Shared test vectors (1536-dim, JSON format)
```

> ⚠️ **Note on Test Files:** All files in the `tests/` directory (`test_vector_store.cpp`, `test_file_manager.cpp`, `test_vector_server.cpp`) **were created by AI**. They cover the core functionality but should be reviewed and extended manually before being treated as authoritative test coverage.

---

## Configuration Constants

Defined in `include/vector_store.h`:

| Constant | Value | Description |
|---|---|---|
| `dimensions_set` | `1536` | Vector dimensionality (OpenAI `ada-002` compatible) |
| `id_length_set` | `32` | Max characters for a vector ID |
| `meta_data_length_set` | `32` | Max characters per metadata key or value |
| `meta_data_kp_pairs_set` | `3` | Max metadata key-value pairs per vector |

---

## Building

> Requires a C++17 compiler (GCC or Clang) and CMake.

```bash
mkdir build && cd build
cmake ..
make
./VectorDB
```

The server starts on port `8080` and auto-loads any existing `data/database.vdb` from disk.

---

## Talking to the Server

You can use `telnet` or `nc` to test the server manually:

```bash
telnet localhost 8080
```

**Insert a vector:**
```
INSERT my_vec 1536 category=ml 0.1 0.2 0.3 ... (1536 floats)
```

**Query top-3 similar vectors:**
```
QUERY 3 0.1 0.2 0.3 ... (1536 floats)
```

**Query with metadata filter:**
```
QUERY 3 category=ml 0.1 0.2 0.3 ...
```

**Delete a vector:**
```
DELETE my_vec
```

**Persist current state to disk:**
```
SAVE
```

---

## .vdb Binary File Format

```
[Header — fixed 32 bytes]
  magic[4]          = 'V','D','B','\0'
  version           = uint8
  dimensions        = uint32
  id_length         = uint8
  kv_length         = uint8
  max_kv            = uint8
  live_vector_count = uint64
  total_vector_count= uint64
  padding[4]

[Record — fixed size, repeated total_vector_count times]
  active_flag       = uint8  (0 = tombstoned / deleted)
  id                = char[id_length]
  metadata          = Metadata_entry[max_kv]  (key[kv_length] + value[kv_length]) × max_kv
  embeddings        = float[dimensions]
```

Records are never physically removed on delete — the `active_flag` is set to `0` (tombstone). Call `compact()` to rewrite the file without deleted records.

---

## What Is Not Yet Implemented

The following are planned for the future but not yet implemented in the codebase:

- **HNSW index** (the primary ANN target; IVF has been implemented instead)
- Multi-threaded client handling (`std::thread` + `std::mutex` on `VectorStore`)
- `BATCH_INSERT` command
- `LIST` command (vector count + memory usage)
- `--port`, `--data-dir`, `--use-hnsw` CLI flags
- AddressSanitizer pass and stress testing
- Python AI layer (Person B): embeddings pipeline, TCP client, RAG chatbot, Streamlit dashboard

---

## Study References

- [Beej's Guide to Network Programming](https://beej.us/guide/bgnet/) — for the TCP server
- [cppreference — std::fstream](https://en.cppreference.com/w/cpp/io/basic_fstream) — for binary I/O
- [Pinecone blog — HNSW explained](https://www.pinecone.io/learn/series/faiss/hnsw/) — ANN background
- [Malkov & Yashunin 2018 (arXiv)](https://arxiv.org/abs/1603.09320) — original HNSW paper
- [Weaviate — What is a vector database](https://weaviate.io/blog/what-is-a-vector-database) — conceptual overview
