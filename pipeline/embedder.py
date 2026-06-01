# =============================================================================
# pipeline/embedder.py
# Converts plain text into a 768-dimensional float vector using the
# nomic-embed-text through ollama
#
# Every piece of text that enters the vector database passes through here —
# both at ingestion time and at query time.
# The same model must be used for both, otherwise similarity scores are wrong.
#
# Requires: ollama, nomic-embed-model, dotenv
# Install:  irm https://ollama.com/install.ps1 | iex 
#           ollama pull nomic-embed-text
#           pip install python-dotenv
# =============================================================================

import os
import sys
import ollama
from dotenv import load_dotenv

load_dotenv()
# Never Mix models between 'insert' and 'query'
model = os.getenv("EMBEDDING_MODEL")

def embed(text: str) -> list[float]:
    """
    Converts a text string into a dimensional embedding vector.
    The vector numerically captures the semantic meaning of the text.

    Args:
        text: any plain-text string (sentence, paragraph, or chunk)

    Returns:
        list of floats — the embedding vector
    
    Raises: 
        error -  unexpected behaviour
    """
    try:
        response = ollama.embed( model = model, input = text)
        if 'embeddings' in response and response['embeddings']:
            return response['embeddings'][0]
        else:
            raise KeyError("Unexpected response format from Ollama API.")
    
    except Exception as e :
        print(f"Error generating embedding with model '{model}': {e}", file=sys.stderr)
        raise e