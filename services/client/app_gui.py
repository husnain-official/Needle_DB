# app_gui.py
# Run: streamlit run services/client/app_gui.py  (from repo root)
# or inside the container: streamlit run services/client/app_gui.py ...

import sys
import os
import tempfile
from dotenv import load_dotenv

sys.path.append(".")
load_dotenv()

import streamlit as st

# ── page config must be first streamlit call ─────────────────────────
st.set_page_config(
    page_title="Needle_DB RAG Chatbot",
    page_icon="🪡",
    layout="wide",
    initial_sidebar_state="expanded",
)

# ── lazy import so app loads even if server is offline ───────────────
def get_bot_class():
    from app.rag_chatbot import RAGChatbot
    return RAGChatbot


# ────────────────────────────────────────────────────────────────────
# CONFIG
# ────────────────────────────────────────────────────────────────────
DEFAULT_IP   = os.getenv("IP") or ""
_env_port    = os.getenv("PORT")
DEFAULT_PORT = int(_env_port) if _env_port else 8080


# ────────────────────────────────────────────────────────────────────
# SESSION STATE
# ────────────────────────────────────────────────────────────────────
def init_state():
    defaults = {
        "bot":                None,
        "connected":          False,
        "chat_history":       [],   # list of {"role": "user"|"bot", "content": str, "sources": list}
        "kb_loaded":          False,
        "server_ip":          DEFAULT_IP,
        "server_port":        DEFAULT_PORT,
        "last_optimize_at":   None,  # chunk count recorded when user last ran OPTIMIZE
        "admin_msg":          None,  # last admin command result message
        "search_results":     [],    # persist last search so delete buttons work across reruns
    }
    for k, v in defaults.items():
        if k not in st.session_state:
            st.session_state[k] = v

init_state()


# ────────────────────────────────────────────────────────────────────
# HELPERS
# ────────────────────────────────────────────────────────────────────
def get_stats():
    if st.session_state.bot is None:
        return {"total_queries": 0, "total_latency_ms": 0.0, "no_context_count": 0, "kb_chunks_loaded": 0}
    return st.session_state.bot.stats


def avg_latency_ms():
    s = get_stats()
    n = s["total_queries"]
    return s["total_latency_ms"] / n if n > 0 else 0.0


def chunk_count():
    """Client-side ledger estimate of accepted chunks (not a live engine COUNT)."""
    if st.session_state.bot is None:
        return 0
    return int(st.session_state.bot.stats.get("kb_chunks_loaded", 0) or 0)


def kb_summary():
    if st.session_state.bot is None:
        return []
    try:
        rows = st.session_state.bot.get_kb_summary()
    except Exception:
        docs = st.session_state.bot.loaded_documents
        rows = [
            {"Document": name, "Status": "—", "Accepted": cnt, "Total": cnt, "Chunks": cnt}
            for name, cnt in sorted(docs.items())
        ]
    for row in rows:
        name = row["Document"]
        if name.lower().endswith(".txt"):
            row["Type"] = "📄 TXT"
        elif name.lower().endswith(".pdf"):
            row["Type"] = "📕 PDF"
        elif name.lower().endswith(".docx"):
            row["Type"] = "📘 DOCX"
        else:
            row["Type"] = "❓"
    return rows


def _engine_schema():
    """Return the engine schema object (from the connected bot) or None."""
    bot = st.session_state.bot
    if bot is None:
        return None
    return getattr(bot, "engine_schema", None)


def optimize_threshold_info():
    """
    Compute display values for the OPTIMIZE progress indicator.

    Engine rule (schema.hpp):
      - Warnings only start once live count >= OPTIMIZE_REM_STARTS_AT (500)
      - After that the engine warns when live ≈ last_build_at * OPTIMIZE_FACTOR (2)

    We do not have last_build_at on the wire, so we use:
      - current  = client ledger chunk count
      - baseline = last time the user pressed OPTIMIZE in this session
                   (falls back to OPTIMIZE_REM_STARTS_AT)
      - next     = max(starts_at, baseline * factor)
    """
    schema = _engine_schema()
    starts_at = int(getattr(schema, "OPTIMIZE_REM_STARTS_AT", 500)) if schema else 500
    factor    = int(getattr(schema, "OPTIMIZE_FACTOR", 2)) if schema else 2
    current   = chunk_count()

    baseline = st.session_state.last_optimize_at
    if baseline is None:
        baseline = starts_at

    next_threshold = max(starts_at, int(baseline) * factor)

    return {
        "current": current,
        "starts_at": starts_at,
        "factor": factor,
        "baseline": baseline,
        "next_threshold": next_threshold,
        "relevant": current >= starts_at,
    }



def run_admin(cmd: str):
    """Execute SAVE / LOAD / OPTIMIZE and store a user-visible message."""
    bot = st.session_state.bot
    if bot is None:
        st.session_state.admin_msg = ("error", "Not connected.")
        return
    try:
        if cmd == "SAVE":
            msg = bot.client.save()
        elif cmd == "LOAD":
            msg = bot.client.load()
        elif cmd == "OPTIMIZE":
            msg = bot.client.optimize()
            # Record current ledger count as the new baseline for the threshold UI
            st.session_state.last_optimize_at = chunk_count()
        else:
            st.session_state.admin_msg = ("error", f"Unknown command {cmd}")
            return
        st.session_state.admin_msg = ("ok", msg.strip())
    except Exception as e:
        st.session_state.admin_msg = ("error", str(e))


# ────────────────────────────────────────────────────────────────────
# SIDEBAR
# ────────────────────────────────────────────────────────────────────
with st.sidebar:
    st.title("🪡 Needle_DB")
    st.caption("v2.0 · Streamlit UI")
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
    port = st.number_input(
        "Port", value=st.session_state.server_port,
        min_value=1024, max_value=65535, step=1, key="port_input"
    )

    col1, col2 = st.columns(2)
    with col1:
        if st.button("Connect", use_container_width=True, type="primary"):
            try:
                RAGChatbot = get_bot_class()
                bot = RAGChatbot(host=ip, port=int(port))
                bot.connect()
                st.session_state.bot         = bot
                st.session_state.connected   = True
                st.session_state.server_ip   = ip
                st.session_state.server_port = int(port)
                st.success("Connected!")
                st.rerun()
            except ConnectionRefusedError:
                st.error("Refused — is the server running?")
            except OSError as e:
                st.error(f"Error: {e}")
            except Exception as e:
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
            st.session_state.search_results = []
            st.rerun()

    st.divider()

    # ── KB quick load ──
    st.subheader("Knowledge Base")
    from schema import PY_SCHEMA
    if st.button(
        "Load KB from folder",
        use_container_width=True,
        disabled=not st.session_state.connected,
        help=f"Ingest all supported files from {PY_SCHEMA.KB_FOLDER}",
    ):
        with st.spinner(f"Loading from '{PY_SCHEMA.KB_FOLDER}'..."):
            try:
                st.session_state.bot.load_knowledge_base(PY_SCHEMA.KB_FOLDER)
                st.session_state.kb_loaded = True
                st.success(f"{chunk_count()} chunks in ledger")
                st.rerun()
            except Exception as e:
                st.error(f"Error: {e}")

    st.divider()

    # ── live stats + optimize indicator ──
    st.subheader("Live Stats")
    s = get_stats()
    current = chunk_count()
    st.metric("Chunks (ledger)", current)
    st.metric("Queries answered", s["total_queries"])
    st.metric("Avg latency (ms)", f"{avg_latency_ms():.0f}")
    st.metric("No-context replies", s["no_context_count"])

    info = optimize_threshold_info()
    st.markdown("**OPTIMIZE threshold**")
    if not st.session_state.connected:
        st.caption("Connect to see threshold.")
    elif not info["relevant"]:
        st.caption(
            f"Current **{info['current']}** / next relevant at "
            f"**{info['starts_at']}** (engine ignores OPTIMIZE warnings below this)."
        )
        st.progress(min(info["current"] / max(info["starts_at"], 1), 1.0))
    else:
        st.caption(
            f"Current **{info['current']}** → next suggested **{info['next_threshold']}**  \n"
            f"(engine warns when live ≈ last_build × {info['factor']}; "
            f"baseline used here = {info['baseline']})"
        )
        # progress toward next threshold
        denom = max(info["next_threshold"] - info["baseline"], 1)
        frac = min(max((info["current"] - info["baseline"]) / denom, 0.0), 1.0)
        st.progress(frac)

    st.divider()

    # ── Engine Admin ──
    st.subheader("Engine Admin")
    st.caption("Talks directly to the C++ engine over TCP.")

    disabled_admin = not st.session_state.connected
    a1, a2 = st.columns(2)
    with a1:
        if st.button(
            "⚡ OPTIMIZE",
            use_container_width=True,
            disabled=disabled_admin,
            help="Rebuild k-means IVF clusters (can take minutes on large DBs).",
        ):
            with st.spinner("Running OPTIMIZE (may take a while)..."):
                run_admin("OPTIMIZE")
            st.rerun()
        if st.button(
            "💾 SAVE",
            use_container_width=True,
            disabled=disabled_admin,
            help="Flush header (live/total counts) to disk.",
        ):
            with st.spinner("Saving..."):
                run_admin("SAVE")
            st.rerun()
    with a2:
        if st.button(
            "📥 LOAD",
            use_container_width=True,
            disabled=disabled_admin,
            help="Reload from disk and rebuild IVF from scratch (can take minutes).",
        ):
            with st.spinner("Running LOAD (may take a while)..."):
                run_admin("LOAD")
            st.rerun()

    # Manual single-ID delete
    with st.expander("🗑 Delete by exact ID", expanded=False):
        del_id = st.text_input(
            "Vector ID",
            placeholder="e.g. kb_ai_ml_chunk_0",
            key="manual_delete_id",
            disabled=disabled_admin,
        )
        if st.button("Delete ID", type="secondary", disabled=disabled_admin or not del_id.strip()):
            try:
                resp = st.session_state.bot.client.delete(del_id.strip())
                st.session_state.admin_msg = ("ok", resp.strip())
                # Best-effort ledger refresh (count may be slightly off until re-ingest logic)
                st.session_state.bot._rehydrate_from_ledger()
            except Exception as e:
                st.session_state.admin_msg = ("error", str(e))
            st.rerun()

    if st.session_state.admin_msg:
        kind, text = st.session_state.admin_msg
        if kind == "ok":
            st.success(text)
        else:
            st.error(text)


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
        st.info("Connect to the server using the sidebar to get started.")
    else:
        if chunk_count() == 0:
            st.info(
                "No chunks in the client ledger yet. Use **Load KB from folder** in the "
                "sidebar or upload a document — otherwise the existing DB is fine to query."
            )

        # ── chat history ──
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

        # ── clear history placed near the input (not at the top) ──
        clear_col, _ = st.columns([1, 4])
        with clear_col:
            if st.button("🗑 Clear chat", key="clear_chat", use_container_width=True):
                st.session_state.chat_history = []
                if st.session_state.bot:
                    st.session_state.bot.clear_history()
                st.rerun()

        # ── chat input ──
        question = st.chat_input("Ask a question about your documents...")

        if question:
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
    st.caption("Search the Needle_DB directly, inspect scores, and delete individual vectors.")

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
            top_k = st.slider("Top-k", min_value=1, max_value=30, value=5)

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
                    st.session_state.search_results = results
                except Exception as e:
                    st.error(f"Search error: {e}")
                    st.session_state.search_results = []

        results = st.session_state.search_results
        if not results:
            st.caption("No results yet — run a search above.")
        else:
            st.success(f"Showing {len(results)} result(s)")
            for rank, (doc_id, score, text) in enumerate(results, 1):
                with st.container(border=True):
                    c1, c2, c3 = st.columns([4, 1, 1])
                    with c1:
                        st.markdown(f"**#{rank} — `{doc_id}`**")
                    with c2:
                        st.markdown(f"**{score:.4f}**")
                    with c3:
                        if st.button(
                            "🗑",
                            key=f"del_result_{rank}_{doc_id}",
                            help=f"Delete vector {doc_id}",
                        ):
                            try:
                                resp = st.session_state.bot.client.delete(doc_id)
                                st.toast(resp.strip())
                                # Remove from the visible list
                                st.session_state.search_results = [
                                    r for r in st.session_state.search_results if r[0] != doc_id
                                ]
                                st.session_state.bot._rehydrate_from_ledger()
                                st.rerun()
                            except Exception as e:
                                st.error(str(e))

                    safe_score = max(0.0, min(float(score), 1.0))
                    st.progress(safe_score, text=f"Similarity: {score:.1%}")
                    if text:
                        st.caption(text)
                    else:
                        st.caption("_(empty text payload)_")


# ════════════════════════════════════════════════════════════════════
# TAB 3 — KNOWLEDGE BASE
# ════════════════════════════════════════════════════════════════════
with tab_kb:
    st.header("Knowledge Base Manager")

    if not st.session_state.connected:
        st.info("Connect to the server first.")
    else:
        # ── 1. Ingest section FIRST (so it stays reachable) ───────────
        st.subheader("⬆ Ingest new documents")
        uploaded = st.file_uploader(
            "Upload a .txt, .pdf, or .docx file",
            type=["txt", "pdf", "docx"],
            help="File will be chunked, embedded, and inserted into Needle_DB. "
                 "A filename already present in the ledger (successful/partial) is rejected.",
        )

        if uploaded:
            st.info(f"Ready to ingest: **{uploaded.name}** ({uploaded.size:,} bytes)")
            if st.button("⬆ Ingest Document", type="primary"):
                suffix = os.path.splitext(uploaded.name)[1]
                with tempfile.NamedTemporaryFile(delete=False, suffix=suffix) as tmp:
                    tmp.write(uploaded.read())
                    tmp_path = tmp.name

                with st.spinner(f"Chunking, embedding, and inserting '{uploaded.name}'..."):
                    try:
                        st.session_state.bot.add_document(tmp_path, display_name=uploaded.name)
                        st.success(f"'{uploaded.name}' finished (see Status column below).")
                    except Exception as e:
                        st.error(f"Ingestion error: {e}")
                    finally:
                        try:
                            os.unlink(tmp_path)
                        except OSError:
                            pass
                st.rerun()

        st.divider()

        # ── 2. Already-ingested table ────────────────────────────────
        st.subheader("Loaded Documents (client ledger)")
        summary = kb_summary()

        if not summary:
            st.warning("No documents in the client ledger yet. Upload above or use the sidebar.")
        else:
            for r in summary:
                name = r["Document"]
                accepted = int(r.get("Accepted", 0) or 0)
                total = int(r.get("Total", 0) or 0)
                status = r.get("Status", "—")
                typ = r.get("Type", "❓")

                with st.container(border=True):
                    st.markdown(f"**{typ} {name}**")
                    st.caption(f"Status: `{status}` · Chunks: **{accepted} / {total}**")

            st.caption(
                f"Total: **{chunk_count()}** accepted chunks across **{len(summary)}** document(s) "
                "(client-side ledger; may lag if another client wrote to the engine)."
            )
            st.caption(
                "To remove vectors: use **Search Explorer** (per-hit 🗑) or **Engine Admin → Delete by exact ID**. "
                "Whole-document bulk delete is not offered in the UI (avoids multi-DELETE + auto-compaction edge cases)."
            )

        st.divider()

        # ── 3. Performance + threshold ───────────────────────────────
        st.subheader("Performance & Index Health")
        s = get_stats()
        c1, c2, c3, c4 = st.columns(4)
        c1.metric("Total Queries",      s["total_queries"])
        c2.metric("Avg Latency (ms)",   f"{avg_latency_ms():.0f}")
        c3.metric("No-context Replies", s["no_context_count"])
        c4.metric("Chunks (ledger)",    chunk_count())

        if s["total_queries"] > 0:
            answered = s["total_queries"] - s["no_context_count"]
            rate = answered / s["total_queries"]
            st.progress(rate, text=f"Answer rate: {rate:.0%} ({answered}/{s['total_queries']} from KB)")

        info = optimize_threshold_info()
        st.markdown("#### IVF / OPTIMIZE status")
        st.write(
            f"Current entries (ledger): **{info['current']}**  \n"
            f"Optimization relevant from: **{info['starts_at']}**  \n"
            f"Suggested next OPTIMIZE around: **{info['next_threshold']}**  \n"
            f"(Engine multiplies last build size by **{info['factor']}**. "
            f"Use the **⚡ OPTIMIZE** button in the sidebar to rebuild clusters.)"
        )
