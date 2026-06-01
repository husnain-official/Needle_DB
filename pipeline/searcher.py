# =============================================================================
# pipeline/searcher.py
# Semantic search helper — embeds a query and searches the vector DB.
#
# This is a thin wrapper around Client.query() that handles embedding
# the query text and optionally filtering by metadata.
#
# Used by test scripts directly.
# The RAGChatbot (app/rag_chatbot.py) calls the client directly for
# tighter control over retrieval scoring.
# =============================================================================

from pipeline.embedder import embed

def semantic_search( client, query_text: str, k: int = 5, filters: dict = None,) -> list:
    """
    Embeds query_text and retrieves the top-k most similar vectors.

    Args:
        client:     connected Client instance
        query_text: plain English query string
        k:          number of results to return (default 5)
        filters:    optional metadata filter dict — up to 3 key=value pairs
                    e.g. {"source": "ai_doc"}
                    e.g. {"source": "wikipedia", "year": "2024"}

    Returns:
        list of (doc_id, score) tuples sorted by score descending
    """
    print(f"\n[searcher] Query : '{query_text}'")

    # ── BUG FIX: original code used 'k' as loop variable inside the
    #    filter_display comprehension, shadowing the k parameter.
    #    Renamed to key_name to avoid the collision.
    if filters:
        filter_display = ", ".join(
            f"{key_name}={val}" for key_name, val in filters.items()
        )
        print(f"[searcher] Filter: {filter_display}")

    vector  = embed(query_text)
    results = client.query(vector, k=k, filters=filters)
    return results


def print_results(results: list):
    """
    Pretty-prints a list of (doc_id, score) tuples.

    Args:
        results: list of (doc_id, score) tuples from semantic_search()
    """
    if not results:
        print("  No results returned.")
        return
    print(f"\n  Top {len(results)} result(s):")
    for rank, (doc_id, score) in enumerate(results, 1):
        print(f"  #{rank:<3} {doc_id:<35} similarity: {score:.4f}")
