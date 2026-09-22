# =============================================================================
# client/vecdb_client.py
# TCP client for the NeedleDB v2 C++ vector database engine.
#
# Protocol: [For all variations, read docs/engine.md]
#
#   INSERT <id> <text_length> <text> <dims> [key=val ...] f1 f2 ... fn
#       -> INSERT <Successful>\n
#       -> INSERT <Successful>, WARNING<OPTIMIZE needed for better searches.>\n
#       -> ERROR <...>\n
#
#   QUERY <top_k> <dims> [key=val ...] f1 f2 ... fn
#       -> QUERY <top_k>\n
#          <id> <similarity_score> <text>\n   (repeated up to top_k times)
#          END\n
#       -> ERROR <...>\n
#
#   DELETE <id>
#       -> DELETE <Successful>\n
#       -> DELETE <Successful>, Compaction<Successful>\n                     (can take minutes on large DBs)
#       -> DELETE <Successful>, WARNING <Database compaction failed>.\n
#       -> ERROR <...>\n
#
#   SAVE\n                                       -> SAVE <Successful>\n
#   LOAD\n                                       -> LOAD <Successful>\n      (can take minutes on large DBs)
#   OPTIMIZE\n                                   -> OPTIMIZE <Successful>\n  (can take minutes on large DBs)
#
# Rules enforced here:
#   - Max sizes for IDs, metadata, and text are all BYTE limits (the engine
#     stores them in fixed-size char[] fields), not Python string/character
#     lengths. Every limit check below validates the UTF-8 encoded length.
#   - Metadata: max schema.META_DATA_KP_PAIRS key=value pairs; each key/value
#     max schema.META_DATA_LENGTH bytes; no spaces, '=', or newlines.
#   - Vectors must be exactly schema.DIMENSIONS floats.
#   - The protocol is strictly line-delimited: ids, metadata, and text must
#     never contain a literal '\n' or '\r', or the command framing breaks.
#   - Commands end with \n (server reads line by line); responses end with \n.
#   - `schema` (a SimpleNamespace) must expose at least: DIMENSIONS,
#     ID_LENGTH, META_DATA_LENGTH, META_DATA_KP_PAIRS, TEXT_MAX_LENGTH,
#     MAX_K_SIMILAR. These come straight from schema.hpp (see engine.md).
#
# =============================================================================

import socket
from types import SimpleNamespace
from schema import PY_SCHEMA


class _Error(Exception):
    """Raised when the NeedleDB server returns an `ERROR <...>` response."""
    pass


class Client:
    """
    TCP client that talks to the C++ engine.
    All protocol formatting and validation lives here so the rest
    of the codebase never has to think about raw sockets.

    Usage:
        schema = SimpleNamespace(
            DIMENSIONS=1024, ID_LENGTH=32, META_DATA_LENGTH=32,
            META_DATA_KP_PAIRS=3, TEXT_MAX_LENGTH=999, MAX_K_SIMILAR=30,
        )
        client = Client()
        client.connect("10.1.177.21", 8080, schema)
        client.insert("doc_0", "Hello World", [0.1, 0.2, ...], metadata={"source": "wiki"})
        results = client.query([0.1, 0.2, ...], k=3, filters={"source": "wiki"})
        client.disconnect()
    """

    DEFAULT_TIMEOUT = PY_SCHEMA.DEFAULT_TIMEOUT  # seconds; fine for INSERT/QUERY/DELETE/SAVE
    LONG_OP_TIMEOUT = PY_SCHEMA.LONG_OP_TIMEOUT  # seconds; LOAD/OPTIMIZE can take minutes

    def __init__(self):
        self.sock = None
        self.schema = None

    # ─────────────────────────────────────────────────────────────────
    # CONNECTION
    # ─────────────────────────────────────────────────────────────────
    def connect(self, host: str, port: int, schema: SimpleNamespace):
        """
        Opens a TCP connection to the C++ engine.

        Args:
            host, port: engine address.
            schema: SimpleNamespace mirroring schema.hpp's constants
                    (DIMENSIONS, ID_LENGTH, META_DATA_LENGTH,
                    META_DATA_KP_PAIRS, TEXT_MAX_LENGTH, MAX_K_SIMILAR, ...).
                    Required -- all validation below depends on it.

        Raises:
            ConnectionRefusedError if the server is not running.
            OSError / TimeoutError for network problems.
        """
        self.schema = schema
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.settimeout(self.DEFAULT_TIMEOUT)
        self.sock.connect((host, port))
        print(f"[Client] Connected to {host}:{port}")

    def disconnect(self):
        """Closes the TCP connection gracefully."""
        if self.sock:
            self.sock.close()
            self.sock = None
            print("[Client] Disconnected")

    def _require_connected(self):
        if self.sock is None:
            raise ConnectionError("Not connected. Call connect() first.")

    # ─────────────────────────────────────────────────────────────────
    # VALIDATION  (enforces protocol limits before any bytes are sent)
    # ─────────────────────────────────────────────────────────────────

    @staticmethod
    def _byte_len(s: str) -> int:
        """UTF-8 byte length -- the unit every engine limit is defined in."""
        return len(s.encode("utf-8"))

    @staticmethod
    def _check_no_framing_chars(label: str, value: str, allow_space: bool = False):
        """
        The wire protocol is line-delimited and space-delimited, so no field
        may contain a literal newline (breaks line framing) or carriage
        return. Fields that are also individually tokenized (ids, metadata
        keys/values) additionally may not contain spaces.
        """
        if "\n" in value or "\r" in value:
            raise ValueError(f"{label} '{value}' contains a newline, which breaks the wire protocol.")
        if not allow_space and " " in value:
            raise ValueError(f"{label} '{value}' contains a space. Use '-' or '_' instead.")

    def _validate_id(self, doc_id: str):
        """
        Validates a document ID against protocol rules.
        1. Validates byte length (max self.schema.ID_LENGTH)
        2. Validates no spaces (use '-' or '_' instead)
        3. Validates no newlines (would break line-delimited framing)
        Raises ValueError if invalid.
        """
        self._check_no_framing_chars("ID", doc_id)
        if self._byte_len(doc_id) > self.schema.ID_LENGTH:
            raise ValueError(
                f"ID '{doc_id}' is too long ({self._byte_len(doc_id)} bytes). "
                f"Max is {self.schema.ID_LENGTH} bytes."
            )

    def _validate_metadata(self, metadata: dict):
        """
        Validates a metadata dict against protocol rules.
        1. Validates number of pairs (max self.schema.META_DATA_KP_PAIRS)
        2. Validates key/value byte lengths (max self.schema.META_DATA_LENGTH)
        3. Validates no spaces, '=', or newlines in keys or values
           (spaces/newlines break tokenizing; '=' is the key/value separator)
        Raises ValueError if invalid.
        """
        if len(metadata) > self.schema.META_DATA_KP_PAIRS:
            raise ValueError(
                f"Too many metadata pairs ({len(metadata)}). Max is {self.schema.META_DATA_KP_PAIRS}."
            )
        for key, val in metadata.items():
            key_str, val_str = str(key), str(val)
            self._check_no_framing_chars("Metadata key", key_str)
            self._check_no_framing_chars("Metadata value", val_str)
            if "=" in key_str or "=" in val_str:
                raise ValueError(
                    f"Metadata key/value cannot contain '=': '{key_str}={val_str}'"
                )
            if self._byte_len(key_str) > self.schema.META_DATA_LENGTH:
                raise ValueError(f"Metadata key '{key_str}' exceeds {self.schema.META_DATA_LENGTH} bytes.")
            if self._byte_len(val_str) > self.schema.META_DATA_LENGTH:
                raise ValueError(f"Metadata value '{val_str}' exceeds {self.schema.META_DATA_LENGTH} bytes.")

    def _validate_text(self, text: str):
        """
        Validates text against protocol rules.
        1. Validates byte length (max self.schema.TEXT_MAX_LENGTH)
        2. Validates no newlines (text is read by byte count server-side,
           but an embedded newline would still corrupt line-based reads of
           anything that echoes this text back, e.g. QUERY results)
        Raises ValueError if invalid.
        """
        self._check_no_framing_chars("Text", text, allow_space=True)
        if self._byte_len(text) > self.schema.TEXT_MAX_LENGTH:
            raise ValueError(
                f"Text is too long ({self._byte_len(text)} bytes). Max is {self.schema.TEXT_MAX_LENGTH} bytes."
            )

    def _validate_embeddings(self, embeddings: list):
        """Validates the vector has exactly schema.DIMENSIONS floats."""
        if len(embeddings) != self.schema.DIMENSIONS:
            raise ValueError(
                f"Expected exactly {self.schema.DIMENSIONS} floats, got {len(embeddings)}."
            )

    def _validate_top_k(self, k: int):
        if k < 1:
            raise ValueError(f"k must be >= 1, got {k}.")
        # if k > self.schema.MAX_K_SIMILAR:
        #     raise ValueError(f"k={k} exceeds MAX_K_SIMILAR ({self.schema.MAX_K_SIMILAR}).")

    # ─────────────────────────────────────────────────────────────────
    # RAW SEND / RECEIVE
    # ─────────────────────────────────────────────────────────────────

    def _send(self, message: str):
        """Encodes and sends a command, appending \\n as protocol requires."""
        self._require_connected()
        self.sock.sendall((message + "\n").encode("utf-8"))

    def _receive(self, timeout: float = None) -> str:
        """
        Reads from the socket until a complete response arrives.
        A QUERY response is complete when the buffer ends with "END\\n";
        every other response is complete at the first "\\n" (this also
        covers ERROR replies to a QUERY, which never start with "QUERY").

        Args:
            timeout: optional override of the socket timeout for this read
                     only (e.g. for LOAD/OPTIMIZE, which can take minutes).
                     The previous timeout is restored afterwards.

        Raises TimeoutError if the server does not respond within the
        active timeout window.
        Raises ConnectionError if the server closes the connection.
        """
        self._require_connected()
        original_timeout = self.sock.gettimeout()
        if timeout is not None:
            self.sock.settimeout(timeout)
        try:
            buffer = b""
            while True:
                try:
                    chunk = self.sock.recv(PY_SCHEMA.RECV_BUFFER_BYTESs)
                except socket.timeout:
                    raise TimeoutError(
                        "Server did not respond in time. "
                        "Check that the engine is still running and reachable."
                    )
                if not chunk:
                    raise ConnectionError("Server closed connection.")

                buffer += chunk

                if buffer.startswith(b"QUERY"):
                    if buffer.endswith(b"END\n"):
                        break
                else:
                    if b"\n" in buffer:
                        break

            return buffer.decode("utf-8", errors="replace").strip()
        finally:
            if timeout is not None:
                self.sock.settimeout(original_timeout)

    @staticmethod
    def _raise_if_error(response: str):
        """Raises _Error if the server responded with ERROR <...>."""
        if response.startswith("ERROR"):
            raise _Error(response)

    # ─────────────────────────────────────────────────────────────────
    # PUBLIC COMMANDS
    # ─────────────────────────────────────────────────────────────────

    def insert(self, entry_id: str, text: str, embeddings: list, metadata: dict = None) -> str:
        """
        Inserts an entry into the engine.

        Wire format:
            INSERT <id> <text_length> <text> <dims> [key=val ...] f1 f2 ... fn\\n

        `text_length` (bytes) and `dims` are derived automatically from
        `text` and `embeddings` -- callers don't pass them.

        Args:
            entry_id: unique identifier, max schema.ID_LENGTH bytes, no spaces
            text:     text payload, max schema.TEXT_MAX_LENGTH bytes
            embeddings: list of exactly schema.DIMENSIONS floats
            metadata: optional dict of up to schema.META_DATA_KP_PAIRS
                      key=value pairs, e.g. {"source": "wiki", "chunk_id": "0"}

        Returns:
            The server's success string, e.g. "INSERT <Successful>" (may
            include a trailing WARNING clause).

        Raises:
            ValueError: a field fails client-side validation.
            _Error: the server returned an ERROR response.
        """
        self._require_connected()
        self._validate_id(entry_id)
        self._validate_text(text)
        self._validate_embeddings(embeddings)

        text_length = self._byte_len(text)
        dims = len(embeddings)
        floats_str = " ".join(str(f) for f in embeddings)

        if metadata:
            self._validate_metadata(metadata)
            meta_str = " ".join(f"{key}={val}" for key, val in metadata.items())
            message = f"INSERT {entry_id} {text_length} {text} {dims} {meta_str} {floats_str}"
        else:
            message = f"INSERT {entry_id} {text_length} {text} {dims} {floats_str}"

        self._send(message)
        response = self._receive()
        self._raise_if_error(response)
        return response

    def query(self, vector: list, k: int = 3, filters: dict = None) -> list:
        """
        Searches the database for the k most similar vectors.

        Wire format:
            QUERY <top_k> <dims> [key=val ...] f1 f2 ... fn\\n
            (filters go AFTER dims, BEFORE floats -- per protocol doc)

        Args:
            vector:  query vector, exactly schema.DIMENSIONS floats
            k:       number of results to return (1 <= k <= schema.MAX_K_SIMILAR)
            filters: optional dict for metadata filtering, e.g.
                     {"source": "wiki"} -- empty values act as wildcards

        Returns:
            list of (doc_id, score, text) tuples, in the order returned
            by the server. `text` preserves the original internal spacing
            (including empty text) exactly as sent by the server.

        Raises:
            ValueError: a field fails client-side validation.
            _Error: the server returned an ERROR response.
        """
        self._require_connected()
        self._validate_top_k(k)
        self._validate_embeddings(vector)

        dims = len(vector)
        floats_str = " ".join(str(f) for f in vector)

        if filters:
            self._validate_metadata(filters)
            filter_str = " ".join(f"{key}={val}" for key, val in filters.items())
            message = f"QUERY {k} {dims} {filter_str} {floats_str}"
        else:
            message = f"QUERY {k} {dims} {floats_str}"

        self._send(message)
        response = self._receive()
        self._raise_if_error(response)

        # Parse "QUERY <top_k>\n id score text\n id score text\n END\n" into a list of tuples.
        # maxsplit=2 preserves any internal spacing in `text` (including empty text),
        # instead of collapsing it the way a plain .split() would.
        results = []
        for line in response.split("\n"):
            if not line or line.startswith("QUERY") or line == "END":
                continue
            parts = line.split(" ", 2)
            if len(parts) < 2:
                continue  # malformed line, skip
            doc_id = parts[0]
            try:
                score = float(parts[1])
            except ValueError:
                continue  # malformed score, skip
            text = parts[2] if len(parts) == 3 else ""
            results.append((doc_id, score, text))
        return results

    def delete(self, doc_id: str) -> str:
        """
        Deletes a vector from the engine by ID.

        Wire format: DELETE <id>\\n
        Returns: the server's success string, e.g. "DELETE <Successful>"
                 (may include a Compaction or WARNING clause).
        Raises:
            ValueError: doc_id fails client-side validation.
            _Error: the server returned an ERROR response.
        """
        self._require_connected()
        self._validate_id(doc_id)
        self._send(f"DELETE {doc_id}")
        response = self._receive()
        self._raise_if_error(response)
        return response

    def save(self) -> str:
        """
        Tells the engine to flush its active header (live/total counts) to disk.

        Wire format: SAVE\\n
        Returns: "SAVE <Successful>"
        Raises: _Error if the server returned an ERROR response.
        """
        self._require_connected()
        self._send("SAVE")
        response = self._receive()
        self._raise_if_error(response)
        return response

    def load(self, timeout: float = LONG_OP_TIMEOUT) -> str:
        """
        Tells the engine to reload its persisted state from disk and rebuild
        the IVF index from scratch.
        NOTE: This is a blocking operation and may take several minutes
        depending on the size of the database.

        Wire format: LOAD\\n
        Args:
            timeout: seconds to wait for a response (default 300s, since
                     the default 15s connection timeout is far too short
                     for a full index rebuild on a large database).
        Returns: "LOAD <Successful>"
        Raises: _Error if the server returned an ERROR response.
        """
        self._require_connected()
        self._send("LOAD")
        response = self._receive(timeout=timeout)
        self._raise_if_error(response)
        return response

    def optimize(self, timeout: float = LONG_OP_TIMEOUT) -> str:
        """
        Tells the engine to recluster the entries (k-means rebuild of the
        IVF index) for better search results.
        NOTE: This is a blocking operation and may take several minutes
        depending on the size of the database.

        Wire format: OPTIMIZE\\n
        Args:
            timeout: seconds to wait for a response (default 300s -- see
                     load() for why the short default timeout doesn't work here).
        Returns: "OPTIMIZE <Successful>"
        Raises: _Error if the server returned an ERROR response.
        """
        self._require_connected()
        self._send("OPTIMIZE")
        response = self._receive(timeout=timeout)
        self._raise_if_error(response)
        return response
    