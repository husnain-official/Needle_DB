# =============================================================================
# client/pipeline/searcher.py
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
from schema import PY_SCHEMA

def semantic_search(
    client,
    query_text: str,
    k: int = PY_SCHEMA.DEFAULT_TOP_K,
    filters: dict = None,
) -> list[tuple[str, float, str]]:
    """
    Embeds query_text and retrieves the top-k most similar vectors.

    Args:
        client:     connected Client instance
        query_text: plain English query string
        k:          number of results to return (default 5, must be
                    between 1 and the engine's MAX_K_SIMILAR)
        filters:    optional metadata filter dict — up to
                    schema.META_DATA_KP_PAIRS (3) key=value pairs
                    e.g. {"source": "ai_doc"}
                    e.g. {"source": "wikipedia", "year": "2024"}

    Returns:
        list of (doc_id, score, text) tuples, in the order returned by
        the engine. Each result's originally stored text is included —
        the v2 protocol adds this field to QUERY results; v1 did not.

    Raises:
        ValueError: query_text failed embedding, or k/filters/vector
            failed client-side validation inside Client.query().
        NeedleDBError: the engine returned an ERROR response.
        EmbeddingDimensionError: the configured embedding model's output
            doesn't match the engine's fixed vector size (see
            pipeline/embedder.py).
    """
    print(f"\n[searcher] Query : '{query_text}'")

    vector = embed(query_text)
    results = client.query(vector, k=k, filters=filters)
    return results


def print_results(results: list[tuple[str, float, str]]):
    """
    Pretty-prints a list of (doc_id, score, text) tuples, including a
    short preview of each result's stored text.

    Args:
        results: list of (doc_id, score, text) tuples from semantic_search()
    """
    if not results:
        print("  No results returned.")
        return

    print(f"\n  Top {len(results)} result(s):")
    for rank, (doc_id, score, text) in enumerate(results, 1):
        print(f"  #{rank:<3} {doc_id:<35} similarity: {score:.4f}")
        if text:
            preview = text if len(text) <= PY_SCHEMA.TEXT_PREVIEW_CHARS else text[:PY_SCHEMA.TEXT_PREVIEW_CHARS - 3] + "..."
            print(f"        {preview}")
