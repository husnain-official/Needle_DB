# app/rag_chatbot.py
# Final RAG Chatbot
#
# Flow for every question:
#   1. embed(question)                  → dim query vector
#   2. client.query(vector, k=5)        → top-5 (doc_id, score) from DataBase
#   3. filter by min_score              → drop irrelevant chunks
#   4. look up chunk text locally       → build context string
#   5. call Local LLM model             → grounded answer from context only  ; Note: Removed.

import os
import time
import json
import ollama
from dotenv import load_dotenv

from client.vecdb_client import Client
from pipeline.embedder import embed
from pipeline.ingestor import chunk_text, read_file, SUPPORTED

load_dotenv()

# Keeps last N exchanges to avoid exceeding GPT context window.
# 1 turn = 1 user message + 1 assistant message = 2 entries.
MAX_HISTORY_TURNS = 6
STORE_FILE = os.getenv("STORE_FILE")
LOCAL_LLM_MODEL = os.getenv("LOCAL_LLM_MODEL")


class RAGChatbot:
    """
    Retrieval-Augmented Generation chatbot backed by vector DataBase.

    Quick start:
        bot = RAGChatbot("10.1.177.21", 8080)
        bot.connect()
        bot.load_knowledge_base("data/documents")
        answer, sources = bot.answer_with_sources("What is machine learning?")
        bot.disconnect()
    """

    def __init__(self, host, port, top_k=5, min_score=0.55):
        """
        Args:
            host:      server IP
            port:      server port (usually 8080)
            top_k:     number of chunks to retrieve per query
            min_score: minimum cosine similarity to accept a chunk as context.
                       Chunks below this are silently dropped.
                       0.3 = lenient, 0.5 = strict grounding.
        """
        self.host      = host
        self.port      = port
        self.top_k     = top_k
        self.min_score = min_score
        self.client    = Client()

        # doc_id → original chunk text
        # server returns IDs + scores only, not text.
        # We keep text locally to build GPT prompts.
        self.chunk_store: dict[str, str] = {}

        # conversation history for multi-turn Q&A
        self.history: list[dict] = []

        # ── ADDITION 1: query stats for demo display ──────────────────
        # Tracks latency and retrieval counts so we can show performance
        # during the demo without running a separate benchmark.
        self.stats = {
            "total_queries":     0,
            "total_latency_ms":  0.0,
            "no_context_count":  0,
        }

    # ─────────────────────────────────────────────────────────────────
    # CONNECTION
    # ─────────────────────────────────────────────────────────────────

    def connect(self):
        """Connect to the C++ server and load local chunk mapping."""
        self.client.connect(self.host, self.port)
        
        # FIXED: Load chunk store from disk if it exists to match persisted DB state
        if os.path.exists(STORE_FILE):
            try:
                with open(STORE_FILE, "r", encoding="utf-8") as f:
                    self.chunk_store = json.load(f)
                print(f"[RAGChatbot] Loaded {len(self.chunk_store)} cached text chunks from disk.")
            except Exception as e:
                print(f"[RAGChatbot] WARNING: Could not load local chunk cache: {e}")
                
        print(f"RAGChatbot connected to {self.host}:{self.port}")

    def disconnect(self):
        """Save DB state, persist local chunk mapping, and close connection."""
        self.client.save()
        
        self._save_chunk_store()

        self.client.disconnect()
        print("RAGChatbot disconnected.")

    # ─────────────────────────────────────────────────────────────────
    # KNOWLEDGE BASE LOADING
    # ─────────────────────────────────────────────────────────────────

    def load_knowledge_base(self, folder_path, chunk_size=150):
        """
        Reads all supported files (.txt, .pdf, .docx) in folder_path,
        chunks each file, embeds every chunk, inserts into Person A's DB,
        and stores chunk text locally for prompt building.

        Safe to call more than once — additional docs are added each time.

        Args:
            folder_path: path to folder containing knowledge base files
            chunk_size:  words per chunk (default 150)
        """
        print(f"\nLoading knowledge base from '{folder_path}'...")

        if not os.path.exists(folder_path):
            print(f"ERROR: folder '{folder_path}' not found.")
            return

        files = [
            f for f in os.listdir(folder_path)
            if os.path.splitext(f)[1].lower() in SUPPORTED
        ]

        if not files:
            print(f"No supported files found. Supported: {', '.join(sorted(SUPPORTED))}")
            return

        print(f"Found {len(files)} file(s): {files}\n")
        total_chunks = 0

        for filename in files:
            filepath    = os.path.join(folder_path, filename)
            source_name = os.path.splitext(filename)[0][:32]

            try:
                text = read_file(filepath)
            except Exception as e:
                print(f"  SKIP '{filename}': {e}")
                continue

            if not text.strip():
                print(f"  SKIP '{filename}': no extractable text.")
                continue

            chunks = chunk_text(text, chunk_size)
            print(f"  '{filename}' → {len(chunks)} chunk(s)")

            for i, chunk in enumerate(chunks):
                doc_id = f"{source_name[:20]}_chunk_{i}"
                doc_id = doc_id[:32]

                metadata = {
                    "source":   source_name,
                    "chunk_id": str(i),
                }

                vector   = embed(chunk)
                response = self.client.insert(doc_id, vector, metadata=metadata)

                if "OK" not in response:
                    print(f"    WARNING: unexpected response for {doc_id}: {response}")

                self.chunk_store[doc_id] = chunk
                total_chunks += 1

        print(f"\nKnowledge base ready — {total_chunks} chunk(s) loaded into DB.")
        # NEW: Save immediately after loading the folder
        if total_chunks > 0:
            self._save_chunk_store()

    # ─────────────────────────────────────────────────────────────────
    # INTERNAL HELPERS
    # ─────────────────────────────────────────────────────────────────

    def _retrieve(self, question, filters=None):
        """
        Embeds question, queries Person A's DB, applies min_score filter,
        looks up chunk text locally.

        Returns list of (doc_id, score, chunk_text).
        """
        vector  = embed(question)
        results = self.client.query(vector, k=self.top_k, filters=filters)

        retrieved = []
        for doc_id, score in results:
            if score < self.min_score:
                continue
            text = self.chunk_store.get(doc_id, "")
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

    def _save_chunk_store(self):
        """Helper to safely serialize the local chunk store to disk."""
        try:
            if STORE_FILE:
                # Ensure the directory exists before saving
                os.makedirs(os.path.dirname(STORE_FILE), exist_ok=True)
                with open(STORE_FILE, "w", encoding="utf-8") as f:
                    json.dump(self.chunk_store, f, ensure_ascii=False, indent=2)
                print("[RAGChatbot] Safely synchronized local chunk store to disk.")
        except Exception as e:
            print(f"[RAGChatbot] ERROR saving chunk store to disk: {e}")

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
        print(f"  KB chunks loaded        : {len(self.chunk_store)}")
        print("─────────────────────────────────────────────────────")

    # ── ADDITION 3: single-file ingestion shortcut ────────────────────
    def add_document(self, filepath, chunk_size=150, display_name=None):
        """
        Adds a single file to the knowledge base without scanning a folder.
        Useful for adding one document after the KB is already loaded.

        Example:
            bot.add_document("data/new_paper.pdf")
        """
        filename    = display_name or os.path.basename(filepath)
        source_name = os.path.splitext(filename)[0][:32]

        try:
            text = read_file(filepath)
        except Exception as e:
            print(f"ERROR reading '{filepath}': {e}")
            return

        if not text.strip():
            print(f"SKIP: no extractable text in '{filepath}'.")
            return

        chunks = chunk_text(text, chunk_size)
        print(f"Adding '{filename}' → {len(chunks)} chunk(s)")

        for i, chunk in enumerate(chunks):
            doc_id = f"{source_name[:20]}_chunk_{i}"
            doc_id = doc_id[:32]
            metadata = {"source": source_name, "chunk_id": str(i)}
            vector   = embed(chunk)
            response = self.client.insert(doc_id, vector, metadata=metadata)
            if "OK" not in response:
                print(f"  WARNING {doc_id}: {response}")
            self.chunk_store[doc_id] = chunk

        print(f"Done. {len(chunks)} chunk(s) added.")
        if chunks:
            self._save_chunk_store()
