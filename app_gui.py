# app_gui.py
# Run: streamlit run app_gui.py

import sys
import os
import tempfile
from dotenv import load_dotenv
sys.path.append(".")
load_dotenv()

import streamlit as st

# ── page config must be first streamlit call ─────────────────────────
st.set_page_config(
    page_title="VectorDB RAG Chatbot",
    page_icon="🧠",
    layout="wide",
    initial_sidebar_state="expanded",
)

# ── lazy import so app loads even if server is offline ───────────────
def get_bot_class():
    from app.rag_chatbot import RAGChatbot
    return RAGChatbot

# ────────────────────────────────────────────────────────────────────
# CONFIG — change these to match servers IP
# ────────────────────────────────────────────────────────────────────
DEFAULT_IP   = os.getenv("IP")
DEFAULT_PORT = os.getenv("PORT")
DEFAULT_PORT = int(DEFAULT_PORT)
KB_FOLDER    = os.getenv("DOC_PATH")


# ────────────────────────────────────────────────────────────────────
# SESSION STATE INITIALISATION
# ────────────────────────────────────────────────────────────────────
def init_state():
    defaults = {
        "bot":          None,
        "connected":    False,
        "chat_history": [],   # list of {"role": "user"|"bot", "content": str, "sources": list}
        "kb_loaded":    False,
        "server_ip":    DEFAULT_IP,
        "server_port":  DEFAULT_PORT,
    }
    for k, v in defaults.items():
        if k not in st.session_state:
            st.session_state[k] = v

init_state()


# ────────────────────────────────────────────────────────────────────
# HELPERS
# ────────────────────────────────────────────────────────────────────
def get_stats():
    """Returns stats dict safely — zeros if bot not connected."""
    if st.session_state.bot is None:
        return {"total_queries": 0, "total_latency_ms": 0.0, "no_context_count": 0}
    return st.session_state.bot.stats


def avg_latency_ms():
    s = get_stats()
    n = s["total_queries"]
    return s["total_latency_ms"] / n if n > 0 else 0.0


def chunk_count():
    if st.session_state.bot is None:
        return 0
    return len(st.session_state.bot.chunk_store)


def kb_summary():
    """
    Returns a list of dicts: {filename, chunks, filetype}
    derived from chunk_store keys.
    """
    if st.session_state.bot is None:
        return []
    store = st.session_state.bot.chunk_store
    sources = {}
    for doc_id in store:
        # doc_id format: sourcename_chunk_N
        parts = doc_id.rsplit("_chunk_", 1)
        src = parts[0] if len(parts) == 2 else doc_id
        sources[src] = sources.get(src, 0) + 1
    return [{"Document": src, "Chunks": cnt, "Type": "—"} for src, cnt in sorted(sources.items())]


# ────────────────────────────────────────────────────────────────────
# SIDEBAR
# ────────────────────────────────────────────────────────────────────
with st.sidebar:
    st.title("🧠 VectorDB RAG")
    st.caption("Person B · Week 6 Dashboard")
    st.divider()

    # ── connection status ──
    if st.session_state.connected:
        st.success("● Server connected")
    else:
        st.error("● Server disconnected")

    st.divider()

    # ── server config ──
    st.subheader("Server")
    ip   = st.text_input("IP Address", value=st.session_state.server_ip, key="ip_input")
    port = st.number_input("Port", value=st.session_state.server_port,
                           min_value=1024, max_value=65535, step=1, key="port_input")

    col1, col2 = st.columns(2)

    with col1:
        if st.button("Connect", use_container_width=True, type="primary"):
            try:
                RAGChatbot = get_bot_class()
                bot = RAGChatbot(host=ip, port=int(port), top_k=5, min_score=0.7)
                bot.connect()
                st.session_state.bot       = bot
                st.session_state.connected = True
                st.session_state.server_ip   = ip
                st.session_state.server_port = int(port)
                st.success("Connected!")
                st.rerun()
            except ConnectionRefusedError:
                st.error("Refused — is server running?")
            except OSError as e:
                st.error(f"Error: {e}")

    with col2:
        if st.button("Disconnect", use_container_width=True):
            if st.session_state.bot:
                try:
                    st.session_state.bot.disconnect()
                except Exception:
                    pass
            st.session_state.bot       = None
            st.session_state.connected = False
            st.session_state.kb_loaded = False
            st.rerun()

    st.divider()

    # ── KB loader ──
    st.subheader("Knowledge Base")
    if st.button("Load KB from folder", use_container_width=True,
                 disabled=not st.session_state.connected):
        with st.spinner(f"Loading from '{KB_FOLDER}'..."):
            try:
                st.session_state.bot.load_knowledge_base(KB_FOLDER)
                st.session_state.kb_loaded = True
                st.success(f"{chunk_count()} chunks loaded")
                st.rerun()
            except Exception as e:
                st.error(f"Error: {e}")

    st.divider()

    # ── live stats ──
    st.subheader("Live Stats")
    s = get_stats()
    st.metric("Chunks in DB",     chunk_count())
    st.metric("Queries answered", s["total_queries"])
    st.metric("Avg latency (ms)", f"{avg_latency_ms():.0f}")
    st.metric("No-context replies", s["no_context_count"])


# ────────────────────────────────────────────────────────────────────
# MAIN TABS
# ────────────────────────────────────────────────────────────────────
tab_chat, tab_search, tab_kb = st.tabs(["💬 Chat", "🔍 Search Explorer", "📚 Knowledge Base"])


# ════════════════════════════════════════════════════════════════════
# TAB 1 — CHAT
# ════════════════════════════════════════════════════════════════════
with tab_chat:
    st.header("Chat with your documents")

    if not st.session_state.connected:
        st.info("Connect to Person A's server using the sidebar to get started.")
    elif chunk_count() == 0:
        st.warning("No chunks loaded. Click 'Load KB from folder' in the sidebar.")
    else:
        # ── clear history button ──
        if st.button("🗑 Clear History", key="clear_chat"):
            st.session_state.chat_history = []
            if st.session_state.bot:
                st.session_state.bot.clear_history()
            st.rerun()

        # ── render chat history ──
        for msg in st.session_state.chat_history:
            if msg["role"] == "user":
                with st.chat_message("user"):
                    st.write(msg["content"])
            else:
                with st.chat_message("assistant"):
                    st.write(msg["content"])
                    if msg.get("sources"):
                        unique_sources = list(dict.fromkeys(
                            s.rsplit("_chunk_", 1)[0] for s in msg["sources"]
                        ))
                        st.caption(f"📄 Sources: {', '.join(unique_sources)}")

        # ── chat input ──
        question = st.chat_input("Ask a question about your documents...")

        if question:
            # show user message immediately
            st.session_state.chat_history.append({
                "role": "user", "content": question, "sources": []
            })

            with st.chat_message("assistant"):
                with st.spinner("Retrieving and generating answer..."):
                    try:
                        answer, sources = st.session_state.bot.answer_with_sources(
                            question, verbose=False
                        )
                    except Exception as e:
                        answer  = f"Error: {e}"
                        sources = []

                st.write(answer)
                if sources:
                    unique = list(dict.fromkeys(
                        s.rsplit("_chunk_", 1)[0] for s in sources
                    ))
                    st.caption(f"📄 Sources: {', '.join(unique)}")

            st.session_state.chat_history.append({
                "role": "bot", "content": answer, "sources": sources
            })
            st.rerun()


# ════════════════════════════════════════════════════════════════════
# TAB 2 — SEARCH EXPLORER
# ════════════════════════════════════════════════════════════════════
with tab_search:
    st.header("Search Explorer")
    st.caption("Search the vector DB directly and inspect raw similarity scores.")

    if not st.session_state.connected:
        st.info("Connect to the server first.")
    else:
        query = st.text_input("Search query", placeholder="e.g. deep learning neural networks")

        col_a, col_b, col_c = st.columns([2, 2, 1])
        with col_a:
            filter_key = st.text_input("Filter key (optional)", placeholder="source")
        with col_b:
            filter_val = st.text_input("Filter value (optional)", placeholder="kb_ai_ml")
        with col_c:
            top_k = st.slider("Top-k", min_value=1, max_value=10, value=5)

        if st.button("🔍 Search", type="primary", disabled=not query):
            filters = None
            if filter_key.strip() and filter_val.strip():
                filters = {filter_key.strip(): filter_val.strip()}

            with st.spinner("Searching..."):
                try:
                    from pipeline.embedder import embed
                    vector  = embed(query)
                    results = st.session_state.bot.client.query(
                        vector, k=top_k, filters=filters
                    )
                except Exception as e:
                    st.error(f"Search error: {e}")
                    results = []

            if not results:
                st.warning("No results found.")
            else:
                st.success(f"Found {len(results)} result(s)")
                for rank, (doc_id, score) in enumerate(results, 1):
                    chunk_preview = st.session_state.bot.chunk_store.get(doc_id, "")
                    # preview       = chunk_preview[:200] + "..." if len(chunk_preview) > 200 else chunk_preview
                    preview = chunk_preview

                    with st.container(border=True):
                        c1, c2 = st.columns([3, 1])
                        with c1:
                            st.markdown(f"**#{rank} — {doc_id}**")
                        with c2:
                            st.markdown(f"Score: **{score:.4f}**")

                        st.progress(min(float(score), 1.0),
                                    text=f"Similarity: {score:.1%}")
                        if preview:
                            st.caption(preview)
                        else:
                            st.caption("_(chunk text not available locally)_")


# ════════════════════════════════════════════════════════════════════
# TAB 3 — KNOWLEDGE BASE
# ════════════════════════════════════════════════════════════════════
with tab_kb:
    st.header("Knowledge Base Manager")

    if not st.session_state.connected:
        st.info("Connect to the server first.")
    else:
        # ── document table ──
        st.subheader("Loaded Documents")
        summary = kb_summary()

        if not summary:
            st.warning("No documents loaded yet. Use the sidebar or upload below.")
        else:
            # add file type column from extension inference
            for row in summary:
                name = row["Document"]
                if name.endswith((".txt",)):
                    row["Type"] = "📄 TXT"
                elif name.endswith((".pdf",)):
                    row["Type"] = "📕 PDF"
                elif name.endswith((".docx",)):
                    row["Type"] = "📘 DOCX"
                else:
                    row["Type"] = "📄 TXT"   # default — source names have no ext
            # st.dataframe(summary, use_container_width=True, hide_index=True)
            st.dataframe(summary, width="stretch", hide_index=True)
            st.caption(f"Total: {chunk_count()} chunks across {len(summary)} document(s)")

        st.divider()

        # ── file uploader ──
        st.subheader("Add a Document")
        uploaded = st.file_uploader(
            "Upload a .txt, .pdf, or .docx file",
            type=["txt", "pdf", "docx"],
            help="File will be chunked, embedded, and inserted into the vector DB."
        )

        if uploaded:
            st.info(f"Ready to ingest: **{uploaded.name}** ({uploaded.size:,} bytes)")

            if st.button("⬆ Ingest Document", type="primary"):
                # save to temp file so ingestor can read it
                suffix = os.path.splitext(uploaded.name)[1]
                with tempfile.NamedTemporaryFile(
                    delete=False, suffix=suffix
                ) as tmp:
                    tmp.write(uploaded.read())
                    tmp_path = tmp.name

                with st.spinner(f"Chunking, embedding, and inserting '{uploaded.name}'..."):
                    try:
                        st.session_state.bot.add_document(tmp_path, chunk_size=150)
                        st.success(f"'{uploaded.name}' ingested successfully!")
                    except Exception as e:
                        st.error(f"Ingestion error: {e}")
                    finally:
                        os.unlink(tmp_path)   # clean up temp file

                st.rerun()

        st.divider()

        # ── performance stats ──
        st.subheader("Performance Stats")
        s = get_stats()
        c1, c2, c3, c4 = st.columns(4)
        c1.metric("Total Queries",      s["total_queries"])
        c2.metric("Avg Latency (ms)",   f"{avg_latency_ms():.0f}")
        c3.metric("No-context Replies", s["no_context_count"])
        c4.metric("Chunks in DB",       chunk_count())

        if s["total_queries"] > 0:
            answered = s["total_queries"] - s["no_context_count"]
            rate = answered / s["total_queries"]
            st.progress(rate, text=f"Answer rate: {rate:.0%} ({answered}/{s['total_queries']} from KB)")
