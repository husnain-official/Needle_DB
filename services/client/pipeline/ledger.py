# =============================================================================
# pipeline/ledger.py
# Client-side ingestion ledger (JSON).
#
# Records what this Python process (and previous runs of it) successfully
# pushed into the engine. The engine itself has no LIST/COUNT command, so
# this is the only way the GUI can show non-zero document / chunk counts
# after a restart.
#
# Structure:
# {
#   "documents": {
#     "report.pdf": {
#       "status": "successful" | "partial" | "failed",
#       "total_chunks": 12,
#       "accepted_chunks": 12,
#       "last_ingested": "2026-09-30T05:00:00+00:00",
#       "error": "..."          # optional, only on failure/partial notes
#     },
#     ...
#   },
#   "stats": {
#     "kb_chunks_loaded": 47
#   }
# }
#
# The aggregate kb_chunks_loaded is always recomputed as the sum of
# accepted_chunks across all documents so it stays consistent.
# =============================================================================

import json
import os
from datetime import datetime, timezone
from schema import PY_SCHEMA


def _ledger_path() -> str:
    """
    Ledger lives in the parent directory of KB_FOLDER (not inside the
    knowledge-base folder itself).
    """
    kb = PY_SCHEMA.KB_FOLDER
    parent = os.path.dirname(kb.rstrip(os.sep))
    if not parent:
        parent = "."
    return os.path.join(parent, "ingestion_ledger.json")


def load_ledger() -> dict:
    """
    Load the ledger from disk. Returns a well-formed dict even if the file
    is missing or corrupt (empty documents + zero stats).
    """
    path = _ledger_path()
    empty = {
        "documents": {},
        "stats": {"kb_chunks_loaded": 0},
    }
    if not os.path.exists(path):
        return empty
    try:
        with open(path, "r", encoding="utf-8") as f:
            data = json.load(f)
        if not isinstance(data, dict):
            return empty
        data.setdefault("documents", {})
        data.setdefault("stats", {})
        data["stats"].setdefault("kb_chunks_loaded", 0)
        if not isinstance(data["documents"], dict):
            data["documents"] = {}
        return data
    except Exception:
        return empty


def save_ledger(data: dict) -> None:
    """Write the ledger atomically enough for a single-client use case."""
    path = _ledger_path()
    parent = os.path.dirname(path)
    if parent:
        os.makedirs(parent, exist_ok=True)
    # Recompute aggregate so callers never have to keep it in sync.
    total_accepted = sum(
        int(info.get("accepted_chunks", 0) or 0)
        for info in data.get("documents", {}).values()
    )
    data.setdefault("stats", {})["kb_chunks_loaded"] = total_accepted
    with open(path, "w", encoding="utf-8") as f:
        json.dump(data, f, indent=2, ensure_ascii=False)


def is_already_ingested(filename: str, ledger: dict | None = None) -> bool:
    """
    True if this filename already has a successful or partial record.
    Failed entries are allowed to be retried.
    """
    if ledger is None:
        ledger = load_ledger()
    info = ledger.get("documents", {}).get(filename)
    if not info:
        return False
    return info.get("status") in ("successful", "partial")


def record_ingestion(
    filename: str,
    status: str,
    total_chunks: int,
    accepted_chunks: int,
    error: str | None = None,
) -> dict:
    """
    Upsert one document record and refresh the aggregate stats.
    Returns the updated ledger dict.
    """
    if status not in ("successful", "partial", "failed"):
        raise ValueError(f"Invalid status '{status}'")
    ledger = load_ledger()
    entry = {
        "status": status,
        "total_chunks": int(total_chunks),
        "accepted_chunks": int(accepted_chunks),
        "last_ingested": datetime.now(timezone.utc).isoformat(),
    }
    if error:
        entry["error"] = str(error)
    ledger.setdefault("documents", {})[filename] = entry
    save_ledger(ledger)
    return ledger


def get_document_summary(ledger: dict | None = None) -> list[dict]:
    """
    Flat list of dicts suitable for the GUI table:
    [{Document, Status, Accepted, Total, Chunks}, ...]
    """
    if ledger is None:
        ledger = load_ledger()
    rows = []
    for name, info in sorted(ledger.get("documents", {}).items()):
        accepted = int(info.get("accepted_chunks", 0) or 0)
        total = int(info.get("total_chunks", 0) or 0)
        rows.append({
            "Document": name,
            "Status": info.get("status", "—"),
            "Accepted": accepted,
            "Total": total,
            "Chunks": accepted,  # backward-compatible key used by older UI bits
        })
    return rows
