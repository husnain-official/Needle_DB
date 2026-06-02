# VectorDB & RAG Architecture Overview

This document provides a high-level technical summary of the custom C++ Vector Database and its accompanying Python Retrieval-Augmented Generation (RAG) ecosystem.

## 1. System Capabilities: What It Currently Does

The system is a fully functional, three-tier architecture that ingests unstructured text, converts it to mathematical representations, stores it in a custom binary database, and uses it to ground a local Large Language Model (LLM). 

**Core Features:**
* **Headless C++ Vector Database:** A custom-built TCP server (`vector_server.cpp`) that listens for raw socket connections. It handles insertion, querying, and deletion of high-dimensional vectors (768-d).
* **Binary Persistent Storage:** Data is flushed to a `.vdb` file using a custom binary schema (`file_manager.cpp`), bypassing heavy abstractions like SQLite or JSON.
* **Inverted File (IVF) Indexing:** Uses K-Means clustering (`ivf.cpp`) to group vectors into centroids, allowing for fast Approximate Nearest Neighbor (ANN) searches rather than O(N) brute-force scans.
* **Python Ingestion Pipeline:** Parses `.txt`, `.pdf`, and `.docx` files, chunks them into ~150-word blocks, and embeds them using Ollama (`nomic-embed-text`).
* **Python TCP Middleware:** A dedicated `vecdb_client.py` handles the raw socket encoding/decoding, abstracting the C++ server from the higher-level logic.
* **Local LLM Chat UI:** A Streamlit dashboard (`app_gui.py`) that acts as the presentation layer, bridging the user's questions with the retrieved database chunks and feeding them to a local `qwen2.5:3b` model.

---

## 2. System Interaction: The Data Flow

The architecture decouples the high-performance storage engine (C++) from the application logic (Python).

### Flow A: Ingestion (Writing Data)
1. **User (Streamlit):** Uploads a PDF.
2. **Python (`ingestor.py`):** Extracts text and splits it into 150-word chunks.
3. **Python (`embedder.py`):** Sends each chunk to the local Ollama daemon to get a 768-d float array.
4. **Python (`vecdb_client.py`):** Formats a string `INSERT doc_id 768 source=pdf [floats...]\n` and sends it over TCP.
5. **C++ (`command_parser.cpp`):** Parses the incoming byte stream.
6. **C++ (`file_manager.cpp` & `vector_store.cpp`):** Writes the record to the `.vdb` disk file and updates the in-RAM `std::vector` and IVF cluster.

### Flow B: Querying (Chatting)
1. **User (Streamlit):** Asks "What is Machine Learning?"
2. **Python (`embedder.py`):** Embeds the question into a 768-d vector.
3. **Python (`vecdb_client.py`):** Sends `QUERY 5 768 [floats...]\n`.
4. **C++ (`ivf.cpp`):** Calculates Euclidean distance against centroids, finds the nearest clusters, and performs dot-product similarity against those candidate vectors.
5. **C++ Server:** Returns the top 5 `doc_id`s and their scores over TCP.
6. **Python (`rag_chatbot.py`):** Looks up the raw text for those IDs from a local JSON cache, filters by a minimum confidence score (e.g., 0.7), and builds a prompt block.
7. **Local LLM (Qwen):** Reads the prompt and generates a grounded response to the UI.

---

## 3. Current Limitations & Technical Debt

While the system is highly capable for a custom build, it has several architectural limitations inherent to its current design:

### A. Single-Threaded Blocking I/O
* **The Constraint:** The C++ `Vector_Server` uses a standard `while(true) { accept(); }` loop. 
* **The Reason:** It handles one client connection at a time. If a client sends a massive chunk of data or stalls mid-transmission, the server blocks. Other clients must wait in the `BACKLOG` queue.
* **Future Fix:** Transition to a multi-threaded connection handler (`std::thread` per client) or an asynchronous event loop (e.g., `epoll` or `select`).

### B. In-Memory Dependency (RAM Bound)
* **The Constraint:** On startup, `run()` loads every single active vector from the disk into the `embeddings_` flat `std::vector` in RAM.
* **The Reason:** It allows for blazing-fast dot-product calculations during queries.
* **The Limitation:** If the database grows to 10 million vectors, it will consume roughly ~30GB of RAM. If RAM exhausts, the OS will kill the server process (OOM Killer).
* **Future Fix:** Implement Memory-Mapped Files (`mmap`) or a paging system to read directly from disk into CPU cache without loading the whole file into RAM.

### C. File Alignment & Crash Vulnerability
* **The Constraint:** The `Header` struct is currently 33 bytes, misaligning the subsequent memory blocks. Furthermore, appending vectors using `ios::end` exposes the system to data corruption if a write is interrupted.
* **The Reason:** Manual memory layout math and EOF appending are fast to implement but lack ACID compliance safeguards.
* **Future Fix:** Align the header to exactly 32 or 64 bytes. Use absolute offsets (`get_record_offset`) for writing to ensure block alignment, and implement a Write-Ahead Log (WAL) or atomic file swaps for crash recovery.

### D. Naive IVF Rebuilding
* **The Constraint:** `ivf.cpp` rebuilds K-Means clusters from scratch upon startup or major operations.
* **The Reason:** Dynamic clustering is mathematically complex. Rebuilding ensures accuracy.
* **The Limitation:** As the database grows, K-Means clustering (even with fixed iterations) will significantly delay server boot times.
* **Future Fix:** Save the centroid coordinates to the `.vdb` file (or a separate index file) so they can be loaded instantly on boot.

### E. Protocol Strictness
* **The Constraint:** The client and server communicate via raw string parsing (`INSERT ...`).
* **The Reason:** Easy to debug via raw telnet/netcat.
* **The Limitation:** Parsing large arrays of floats from strings is CPU-intensive and prone to precision loss. 
* **Future Fix:** Move to a pure binary protocol over TCP (e.g., sending raw byte arrays of IEEE 754 floats) to skip the `std::stof` conversion entirely.
