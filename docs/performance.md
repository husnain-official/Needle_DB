# NeedleDB v2 — Performance & Test Report

This document records **correctness test outcomes**, **engine-side search benchmarks**, and a plain explanation of **why interactive chat often feels ~10 seconds slow on CPU-only machines**.

Numbers below are machine-dependent. Treat them as order-of-magnitude evidence that the IVF path is far cheaper than brute force, not as a guarantee on your laptop.

For architecture, see [`engine.md`](./engine.md). For the Python path that wraps embedding + LLM calls, see [`middleware.md`](./middleware.md).

---

## Correctness summary

All automated engine suites listed here were run as gating checks for the v2 engine.

| Suite | Focus | Result |
| :--- | :--- | :--- |
| `command_parser_tests` | INSERT / QUERY / DELETE / SAVE (and related) parsing | **48 / 48** passed |
| `file_manager_tests` | File init, schema checks, read/write/delete, compaction, index persistence | **41 / 41** passed |
| `ivf_tests` | Distances, build, probe search, add/delete lifecycle | **18 / 18** passed |
| `vector_store_tests` | Removal, metadata match, normalization, similarity, id helpers | **31 / 31** passed |
| `vector_store_ivf_integration_tests` | Store ↔ IVF insert/delete propagation, churn, rebuild | **10 / 10** passed |
| `vector_server_tests` | TCP command integration, concurrency, compaction, persistence, bad-index fallback | **33 / 33** passed |

**Total: 181 / 181 passed (0 failures).**

These tests exercise the C++ engine and protocol behaviour. They are not a substitute for end-to-end Docker + Ollama smoke tests on a clean machine.

---

## Engine search benchmarks (curiosity / non-gating)

### IVF vs brute force — isolated index/store tests

**Setup:** `N = 50 000` vectors, `dims = 1024`, `nlist = 100`, `nprobe = 5`.

| Metric | Time |
| :--- | :--- |
| `index.build_()` (full build in this harness) | ~305 805 ms (~5.1 min) |

**Single query**

| Method | Time | Relative |
| :--- | :--- | ---: |
| Brute-force search | ~1694 ms | 1× |
| IVF `search_` | ~88.5 ms | **~19× faster** |

**100 queries**

| Method | Total | Per query |
| :--- | :--- | :--- |
| Brute force | ~136 568 ms | ~1366 ms |
| IVF | ~7327 ms | ~73 ms |

### Server-level comparison (same order of magnitude)

**Setup:** `N = 50 000`, repetitions = 5, max centroids = 100, `nprobe = 5`.

| Metric | Time |
| :--- | :--- |
| Brute-force average search | ~859 ms |
| Fresh IVF `build_()` | ~280 742 ms |
| Fresh IVF average search | ~73 ms |
| Load existing centroids (skip k-means) | ~25 359 ms |
| Search after loaded centroids | ~60 ms |

**Takeaway:** Once centroids exist, a 50k×1024 query is on the order of **tens of milliseconds** inside the engine for these parameters — not multiple seconds.

### OPTIMIZE growth stages (informational)

Average search time before vs after `OPTIMIZE` at increasing sizes (same machine class as above):

| Stage | Vector count | Pre-OPTIMIZE (ms) | Post-OPTIMIZE (ms) | Δ |
| ---: | ---: | ---: | ---: | ---: |
| 1 | 4 000 | 220 | 223 | +1% |
| 2 | 8 000 | 237 | 222 | −6% |
| 3 | 16 000 | 280 | 266 | −5% |
| 4 | 32 000 | 285 | 322 | +13% |
| 5 | 64 000 | 405 | 344 | −15% |

At modest sizes the gain is small or noisy; `OPTIMIZE` matters more as the distribution drifts from the last centroid build and as `N` grows. The engine’s insert-time WARNING uses `OPTIMIZE_REM_STARTS_AT` (500) and `OPTIMIZE_FACTOR` (2) relative to `last_build_at`.

---

## Why chat latency is often ~10 seconds without a GPU

Users feel latency on **Ask a question → answer appears**, not on the IVF microbenchmarks above. That path is longer than a single `QUERY`.

### End-to-end chat path

```text
Question
  → embed query          (Ollama HTTP, mxbai-embed-large)
  → TCP QUERY            (C++ IVF / store)
  → filter by min score  (Python)
  → build prompt         (Python)
  → LLM generate         (Ollama HTTP, qwen2.5:3b, token stream)
  → render answer        (Streamlit)
```

### Where the time goes (CPU-only, typical)

| Stage | What actually runs | Order of magnitude on CPU |
| :--- | :--- | :--- |
| **Embed query** | Full forward pass of the embedding model via Ollama | often **~1–3+ s** |
| **Engine `QUERY`** | IVF probe + dot products on candidates | often **~0.05–0.2 s** at tens of thousands of vectors (see tables above) |
| **Prompt assembly** | String work in Python | usually **&lt; 50 ms** |
| **LLM generation** | Autoregressive decode of `qwen2.5:3b` on CPU | often **~5–15+ s** depending on answer length and CPU |
| **UI overhead** | Streamlit rerun / network to browser | usually small next to the LLM |

So a **~10 second** round trip on a laptop without a GPU is **normal**, and it is usually **not** evidence that the vector index is slow.

Rough split many people will see:

```text
~10 s total  ≈  ~1–3 s embed  +  ≪1 s search  +  ~6–12 s LLM
```

### Why GPU changes the story

- Embedding and generation are neural nets. On GPU, Ollama can cut those stages sharply.
- IVF search is classical linear algebra over a limited candidate set; it is already comparatively cheap on CPU at the sizes tested.
- Therefore attaching a GPU helps **chat and ingest** far more than it helps **raw `QUERY`** microbenchmarks.

### Ingest (many chunks) vs one chat turn

Ingesting hundreds of chunks can take **minutes** because each chunk pays an **embed + INSERT** cost. The slow part is almost always repeated embedding. Each `INSERT` only briefly holds the engine mutex; other TCP clients can still `QUERY` between chunks (see concurrency in [`engine.md`](./engine.md)).

Streamlit’s own session may still look “busy” during a long spinner even though the engine is not globally locked for the whole fifteen minutes.

---

## How to interpret these numbers when testing on a new machine

1. **Cold Docker / Ollama** — first run downloads images and pulls models (~2.4 GB for the default pair). That is install time, not query latency.
2. **Model load** — first embed/generate after Ollama starts can be slower while weights load into memory.
3. **Compare fairly** — time `QUERY` alone (Search Explorer) vs full chat. If search returns quickly but chat is slow, look at Ollama CPU usage, not IVF.
4. **Scale** — at 50k×1024 the engine search numbers above apply only as a reference; your corpus size, `nprobe`, and whether centroids are stale will move the needle.
5. **Correctness first** — the 181/181 suite result is about behavioural correctness of the engine build you compiled, not about your CPU’s LLM tokens/second.

---

## Related knobs

| Knob | Location | Effect on perceived speed |
| :--- | :--- | :--- |
| `top_k` / `DEFAULT_TOP_K` | UI / `PY_SCHEMA` | Larger context → more prompt tokens → slower generation. |
| `DEFAULT_MIN_SCORE` | `PY_SCHEMA` | Higher threshold → fewer chunks → shorter prompts. |
| `OPTIMIZE` | Engine admin / protocol | Refreshes centroids; can improve search quality/latency when data grew a lot since `last_build_at`. |
| Hardware | Host | GPU or stronger CPU helps Ollama stages most. |
| Model choice | `.env` | Smaller/faster LLM trades quality for latency. |

---

## File map

| Artifact | Role |
| :--- | :--- |
| Engine GoogleTest binaries under `services/engine/tests/` | Source of the 181 correctness results |
| This document | Human-readable performance + latency narrative for releases |
| [`middleware.md`](./middleware.md) | Where embed/LLM calls sit in the stack |
| [`engine.md`](./engine.md) | Why IVF search cost stays bounded |
