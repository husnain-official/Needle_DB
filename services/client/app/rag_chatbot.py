# app/rag_chatbot.py
# Final RAG Chatbot
#
# Flow for every question:
#   1. embed(question)                  → dim query vector
#   2. client.query(vector, k=5)        → top-k (doc_id, score, text) from DataBase
#   3. filter by min_score              → drop irrelevant chunks (client-side —
#                                          the engine's QUERY protocol has no
#                                          score-threshold parameter, see engine.md)
#   4. call Local LLM model             → grounded answer from context only

import os
import time
import ollama
from dotenv import load_dotenv

from vecdb_client import Client
from pipeline.embedder import embed
from pipeline.ingestor import (
    chunk_text,
    read_file,
    ingest_folder,
    _sanitize_for_wire,
    _truncate_utf8,
    _build_doc_id,
)
from schema import PY_SCHEMA
from schema_loader import load_cpp_schema

load_dotenv()

# Keeps last N exchanges to avoid exceeding GPT context window.
# 1 turn = 1 user message + 1 assistant message = 2 entries.
MAX_HISTORY_TURNS = PY_SCHEMA.DEFAULT_MAX_HISTORY_TURNS

# host/port default to the environment, same "no default, no fallback"
# philosophy pipeline/embedder.py uses for EMBEDDING_MODEL: if unset, connect()
# will fail loudly rather than silently pointing at the wrong engine.
ENGINE_HOST = os.getenv("ENGINE_HOST")
ENGINE_PORT = int(os.getenv("ENGINE_PORT")) if os.getenv("ENGINE_PORT") else None
LOCAL_LLM_MODEL = os.getenv("LOCAL_LLM_MODEL")


class RAGChatbot:
    """
    Retrieval-Augmented Generation chatbot backed by vector DataBase.

    Quick start:
        bot = RAGChatbot()   # host/port from ENGINE_HOST / ENGINE_PORT env vars
        bot.connect()
        bot.load_knowledge_base("data/documents")
        answer, sources = bot.answer_with_sources("What is machine learning?")
        bot.disconnect()
    """

    def __init__(self, host=None, port=None, top_k=None, min_score=None):
        """
        Args:
            host:      server IP (default: ENGINE_HOST env var)
            port:      server port, usually 8080 (default: ENGINE_PORT env var)
            top_k:     number of chunks to retrieve per query
                       (default: PY_SCHEMA.DEFAULT_TOP_K)
            min_score: minimum cosine similarity to accept a chunk as context.
                       Chunks below this are silently dropped
                       (default: PY_SCHEMA.DEFAULT_MIN_SCORE). This check stays
                       client-side — the engine's QUERY protocol only takes
                       top_k/dims/metadata filters, no similarity threshold.
        """
        self.host      = host if host is not None else ENGINE_HOST
        self.port      = port if port is not None else ENGINE_PORT
        self.top_k     = top_k if top_k is not None else PY_SCHEMA.DEFAULT_TOP_K
        self.min_score = min_score if min_score is not None else PY_SCHEMA.DEFAULT_MIN_SCORE
        self.client    = Client()

        # Populated in connect() via schema_loader.load_cpp_schema() — required
        # by Client.connect()/insert()/query() for all wire-format validation.
        self.engine_schema = None

        # Lightweight per-document chunk counts (filename → chunk count) for
        # UIs that want a "loaded documents" table — NOT the chunk text
        # itself (v2 doesn't cache that; QUERY already returns it). Like
        # everything else here, this is session-scoped: nothing is persisted
        # to disk, so it resets on every connect() and won't reflect data
        # already in the engine from a prior session until reloaded.
        self.loaded_documents: dict[str, int] = {}

        # conversation history for multi-turn Q&A
        self.history: list[dict] = []

        # ── ADDITION 1: query stats for demo display ──────────────────
        # Tracks latency and retrieval counts so we can show performance
        # during the demo without running a separate benchmark.
        self.stats = {
            "total_queries":     0,
            "total_latency_ms":  0.0,
            "no_context_count":  0,
            "kb_chunks_loaded":  0,
        }

    # ─────────────────────────────────────────────────────────────────
    # CONNECTION
    # ─────────────────────────────────────────────────────────────────

    def connect(self):
        """Load the engine's schema and connect to the C++ engine."""
        self.engine_schema = load_cpp_schema()
        self.client.connect(self.host, self.port, self.engine_schema)
        print(f"RAGChatbot connected to {self.host}:{self.port}")

    def disconnect(self):
        """Flush the engine's active header to disk and close the connection."""
        self.client.save()
        self.client.disconnect()
        print("RAGChatbot disconnected.")

    # ─────────────────────────────────────────────────────────────────
    # KNOWLEDGE BASE LOADING
    # ─────────────────────────────────────────────────────────────────

    def load_knowledge_base(self, folder_path, chunk_size=None):
        """
        Reads all supported files (.txt, .pdf, .docx) in folder_path, chunks
        each file, embeds every chunk, and inserts it into the DB.

        Delegates to pipeline.ingestor.ingest_folder(), which builds
        schema-aware doc IDs/metadata and validates every chunk against
        self.engine_schema before sending it to the engine — text no longer
        needs to be cached locally, since v2 QUERY results already include it.

        Safe to call more than once — additional docs are added each time.

        Args:
            folder_path: path to folder containing knowledge base files
            chunk_size:  words per chunk (default: PY_SCHEMA.DEFAULT_CHUNK_SIZE)
        """
        print(f"\nLoading knowledge base from '{folder_path}'...")

        size = chunk_size if chunk_size is not None else PY_SCHEMA.DEFAULT_CHUNK_SIZE
        results = ingest_folder(self.client, folder_path, self.engine_schema, size)

        total_chunks = sum(len(ids) for ids in results.values())
        self.stats["kb_chunks_loaded"] += total_chunks
        for filename, ids in results.items():
            if ids:
                self.loaded_documents[filename] = self.loaded_documents.get(filename, 0) + len(ids)

    # ─────────────────────────────────────────────────────────────────
    # INTERNAL HELPERS
    # ─────────────────────────────────────────────────────────────────

    def _retrieve(self, question, filters=None):
        """
        Embeds question, queries DB, applies min_score filter.

        v2: client.query() already returns (doc_id, score, text) tuples —
        the engine's QUERY response carries each chunk's stored text, so
        there's no local chunk_store lookup anymore.

        Returns list of (doc_id, score, chunk_text).
        """
        vector  = embed(question)
        results = self.client.query(vector, k=self.top_k, filters=filters)

        retrieved = []
        for doc_id, score, text in results:
            if score < self.min_score:
                continue
            if text:
                retrieved.append((doc_id, score, text))

        return retrieved

    def _build_prompt(self, question, retrieved_chunks):
        """
        Builds system + user prompt per roadmap spec.
        Each chunk is numbered and labelled with its source and score.
        """
        context_parts = []
        for i, (doc_id, score, text) in enumerate(retrieved_chunks, 1):
            context_parts.append(
                f"[Source {i}: {doc_id} | similarity: {score:.4f}]\n{text}"
            )
        context = "\n\n".join(context_parts)

        system_prompt = (
            "You are a precise and helpful assistant. "
            "Answer questions strictly using only the provided context. "
            "If the context does not contain enough information to answer, respond: "
            "'I don't have information on that in my knowledge base.' "
            "Do not use any outside knowledge. "
            "Cite the source numbers (e.g. Source 1, Source 2) in your answer."
        )

        user_prompt = (
            f"Context:\n{context}\n\n"
            f"Question: {question}\n\n"
            f"Answer using only the context above:"
        )

        return system_prompt, user_prompt

    def _trim_history(self):
        """Keep only the last MAX_HISTORY_TURNS exchanges."""
        max_messages = MAX_HISTORY_TURNS * 2
        if len(self.history) > max_messages:
            self.history = self.history[-max_messages:]

    def _call_local_llm(self, system_prompt, current_user_payload):
        """Calls local Ollama daemon using Qwen 2.5 3B with explicit message context handling."""
        messages = (
            [{"role": "system", "content": system_prompt}]
            + self.history
            + [{"role": "user", "content": current_user_payload}]
        )
        try:
            # Swapped completely to local execution loop via Ollama
            response = ollama.chat(
                model=LOCAL_LLM_MODEL,
                messages=messages,
                options={
                    "temperature": 0.2,  # Low temperature preserves deterministic factual anchoring
                    "num_predict": 500   # Limits maximum sequence tokens generated
                }
            )
            return response['message']['content'].strip()
        except Exception as e:
            return f"ERROR calling local Ollama daemon: {e}"

    # ─────────────────────────────────────────────────────────────────
    # PUBLIC API
    # ─────────────────────────────────────────────────────────────────

    def answer_with_sources(self, question, filters=None, verbose=True):
        t_start = time.perf_counter()

        # Retrieve vectors from local C++ engine
        retrieved = self._retrieve(question, filters=filters)

        if not retrieved:
            self.stats["no_context_count"] += 1
            msg = "I don't have information on that in my knowledge base."
            return msg, []

        if verbose:
            print(f"  Retrieved {len(retrieved)} chunk(s) (min_score={self.min_score}):")
            for doc_id, score, _ in retrieved:
                print(f"    {doc_id}  score={score:.4f}")

        system_prompt, user_prompt = self._build_prompt(question, retrieved)

        # Dispatch execution payload directly to local model container
        answer_text = self._call_local_llm(system_prompt, user_prompt)

        # Append only pure semantic variables to conversational history state
        self.history.append({"role": "user", "content": question})
        self.history.append({"role": "assistant", "content": answer_text})
        self._trim_history()

        # Capture complete end-to-end processing execution time
        latency_ms = (time.perf_counter() - t_start) * 1000
        self.stats["total_queries"]    += 1
        self.stats["total_latency_ms"] += latency_ms

        sources = [doc_id for doc_id, _, _ in retrieved]
        return answer_text, sources

    def answer(self, question, filters=None, verbose=True):
        """Convenience wrapper — returns answer string only."""
        answer_text, _ = self.answer_with_sources(
            question, filters=filters, verbose=verbose
        )
        return answer_text

    def clear_history(self):
        """Reset conversation history — start a fresh session."""
        self.history = []

    # ── ADDITION 2: performance summary ──────────────────────────────
    def print_stats(self):
        """
        Prints a quick performance summary.
        Call this after your evaluation questions to show demo numbers.
        """
        n = self.stats["total_queries"]
        if n == 0:
            print("No queries run yet.")
            return
        avg = self.stats["total_latency_ms"] / n
        print("\n── RAGChatbot Performance Summary ──────────────────")
        print(f"  Total queries answered  : {n}")
        print(f"  No-context responses    : {self.stats['no_context_count']}")
        print(f"  Avg end-to-end latency  : {avg:.0f} ms")
        print(f"  KB chunks loaded        : {self.stats['kb_chunks_loaded']}")
        print("─────────────────────────────────────────────────────")

    # ── ADDITION 3: single-file ingestion shortcut ────────────────────
    def add_document(self, filepath, chunk_size=None, display_name=None):
        """
        Adds a single file to the knowledge base without scanning a folder.
        Useful for adding one document after the KB is already loaded.

        Mirrors pipeline.ingestor.ingest_file() (same schema-aware doc_id /
        metadata construction, same per-chunk TEXT_MAX_LENGTH check), with
        one addition ingest_file() doesn't support: an optional display_name
        to label the source as something other than the file's real name.

        Example:
            bot.add_document("data/new_paper.pdf")
        """
        filename  = display_name or os.path.basename(filepath)
        base_name = _sanitize_for_wire(os.path.splitext(filename)[0])
        source_for_metadata = _truncate_utf8(base_name, self.engine_schema.META_DATA_LENGTH)
        size = chunk_size if chunk_size is not None else PY_SCHEMA.DEFAULT_CHUNK_SIZE

        try:
            text = read_file(filepath)
        except Exception as e:
            print(f"ERROR reading '{filepath}': {e}")
            return

        if not text.strip():
            print(f"SKIP: no extractable text in '{filepath}'.")
            return

        chunks = chunk_text(text, size)
        print(f"Adding '{filename}' → {len(chunks)} chunk(s)")

        inserted = 0
        for i, chunk in enumerate(chunks):
            chunk_bytes = len(chunk.encode("utf-8"))
            if chunk_bytes > self.engine_schema.TEXT_MAX_LENGTH:
                print(
                    f"  [{i+1}/{len(chunks)}] SKIP — chunk is {chunk_bytes} bytes, "
                    f"exceeds engine limit of {self.engine_schema.TEXT_MAX_LENGTH} bytes."
                )
                continue

            doc_id   = _build_doc_id(base_name, i, self.engine_schema)
            metadata = {"source": source_for_metadata, "chunk_id": str(i)}

            try:
                vector   = embed(chunk)
                response = self.client.insert(doc_id, chunk, vector, metadata=metadata)
            except Exception as e:
                print(f"  [{i+1}/{len(chunks)}] ERROR inserting '{doc_id}': {e}")
                continue

            print(f"  [{i+1}/{len(chunks)}] Server → {response.strip()}")
            inserted += 1

        self.stats["kb_chunks_loaded"] += inserted
        if inserted:
            self.loaded_documents[filename] = self.loaded_documents.get(filename, 0) + inserted
        print(f"Done. {inserted}/{len(chunks)} chunk(s) added.")
