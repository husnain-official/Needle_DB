# =============================================================================
# pipeline/embedder.py
# Converts plain text into a float vector using whichever embedding model
# is configured via the environment, served through Ollama.
#
# Every piece of text that enters the vector database passes through here —
# both at ingestion time and at query time.
# The same model must be used for both, otherwise similarity scores are wrong.
#
# This file is not the source of truth for anything: it makes no assumptions
# about which model to use or what dimensionality the resulting vector has.
# Model selection comes exclusively from the environment (.env / EMBEDDING_MODEL)
# at runtime — no default, no fallback. Engine/schema constants such as vector
# dimensionality live in schema_loader.py and are enforced where they're
# actually authoritative (Client.insert()/query() in vecdb_client.py, against
# the schema it's connected with) — not here, and not redundantly.
#
# Requires: ollama, python-dotenv
# =============================================================================

import os
import sys
import ollama
from dotenv import load_dotenv

load_dotenv()

# Read strictly from the environment — no hardcoded default. If EMBEDDING_MODEL
# isn't set, `model` is None and the Ollama call below will fail loudly with
# that fact surfaced in the error, rather than silently assuming a model.
# Never mix models between 'insert' and 'query'.
model = os.getenv("EMBEDDING_MODEL")


def embed(text: str) -> list[float]:
    """
    Converts a text string into an embedding vector using whichever model
    is configured via the EMBEDDING_MODEL environment variable.

    The vector numerically captures the semantic meaning of the text.
    This function does not assume, check, or validate a vector length —
    dimensionality is whatever the configured model produces. Enforcing
    it against the engine's actual schema is Client.insert()/query()'s
    job (vecdb_client.py), against the schema supplied to Client.connect(),
    which in turn comes from schema_loader.py — not this file.

    Args:
        text: any plain-text string (sentence, paragraph, or chunk)

    Returns:
        list of floats — the embedding vector, exactly as returned by
        the configured model.

    Raises:
        Exception: any failure calling Ollama (EMBEDDING_MODEL unset or
            misconfigured, unreachable Ollama server, unexpected response
            shape, etc.) is logged to stderr and re-raised as-is.
    """
    try:
        response = ollama.embed(model=model, input=text)
        if 'embeddings' in response and response['embeddings']:
            return response['embeddings'][0]
        else:
            raise KeyError("Unexpected response format from Ollama API.")

    except Exception as e:
        print(f"Error generating embedding with model '{model}': {e}", file=sys.stderr)
        raise e
