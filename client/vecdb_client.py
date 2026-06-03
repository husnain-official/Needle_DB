# =============================================================================
# client/vecdb_client.py
# TCP client for Person A's C++ vector database server.
#
# Protocol:
#   INSERT id dims [key=val ...] f1 f2 ... fn\n  → OK\n
#   QUERY  k  dims [key=val ...] f1 f2 ... fn\n  → RESULTS k\n id score\n ... END\n
#   DELETE id\n                                  → OK\n
#   LOAD\n                                       → OK\n
#   SAVE\n                                       → OK\n
#
# Rules enforced here:
#   - ID max 32 chars, no spaces
#   - Metadata max 3 key=value pairs, keys/values max 32 chars, no spaces
#   - Commands end with \n (server reads line by line)
#   - Responses end with \n
# =============================================================================

import socket

class Client:
    """
    TCP client that talks to vector database server.
    All protocol formatting and validation lives here so the rest
    of the codebase never has to think about raw sockets.

    Usage:
        client = VecDBClient()
        client.connect("10.1.177.21", 8080)
        client.insert("doc_0", [0.1, 0.2, 0.9], metadata={"source": "wiki"})
        results = client.query([0.1, 0.2, 0.8], k=3)
        client.disconnect()
    """

    def __init__(self):
        self.sock = None

    # ─────────────────────────────────────────────────────────────────
    # CONNECTION
    # ─────────────────────────────────────────────────────────────────

    def connect(self, host: str, port: int):
        """
        Opens TCP connection to Person A's server.
        Raises ConnectionRefusedError if server is not running.
        Raises OSError / TimeoutError for network problems.
        """
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.settimeout(15)   # 15s — large vectors take time to transmit
        self.sock.connect((host, port))
        print(f"[Client] Connected to {host}:{port}")

    def disconnect(self):
        """Closes the TCP connection gracefully."""
        if self.sock:
            self.sock.close()
            self.sock = None
            print("[Client] Disconnected")

    # ─────────────────────────────────────────────────────────────────
    # VALIDATION  (enforces protocol limits before any bytes are sent)
    # ─────────────────────────────────────────────────────────────────

    def _validate_id(self, doc_id: str):
        """
        Validates a document ID against protocol rules.
        Raises ValueError if invalid.
        """
        if len(doc_id) > 32:
            raise ValueError(
                f"ID '{doc_id}' is too long ({len(doc_id)} chars). Max is 32."
            )
        if " " in doc_id:
            raise ValueError(
                f"ID '{doc_id}' contains a space. Use '-' or '_' instead."
            )

    def _validate_metadata(self, metadata: dict):
        """
        Validates metadata dict against protocol rules.
        Raises ValueError if invalid.
        Rules: max 3 pairs, keys and values max 32 chars, no spaces allowed.
        """
        if len(metadata) > 3:
            raise ValueError(
                f"Too many metadata pairs ({len(metadata)}). Max is 3."
            )
        for k, v in metadata.items():
            k_str, v_str = str(k), str(v)
            if len(k_str) > 32:
                raise ValueError(f"Metadata key '{k_str}' exceeds 32 characters.")
            if len(v_str) > 32:
                raise ValueError(f"Metadata value '{v_str}' exceeds 32 characters.")
            if " " in k_str or " " in v_str:
                raise ValueError(
                    f"Metadata key/value cannot contain spaces: '{k_str}={v_str}'"
                )

    # ─────────────────────────────────────────────────────────────────
    # RAW SEND / RECEIVE
    # ─────────────────────────────────────────────────────────────────

    def _send(self, message: str):
        """Encodes and sends a command, appending \\n as protocol requires."""
        self.sock.sendall((message + "\n").encode())

    def _receive(self) -> str:
        """
        Reads from the socket until a complete response arrives.
        Responses are complete when they contain OK\\n or END\\n or a <ERROR>(...)\\n.
        Raises TimeoutError if server does not respond within 15s.
        Raises ConnectionError if server closes connection unexpectedly.
        """
        buffer = b""
        while True:
            try:
                chunk = self.sock.recv(4096)
            except socket.timeout:
                raise TimeoutError(
                    "Server did not respond within 15 seconds. "
                    "Check that Person A's server is still running."
                )
            if not chunk:
                raise ConnectionError(
                    "Server closed the connection without sending a response."
                )
            buffer += chunk
            decoded = buffer.decode()
            # Both terminal conditions the server can send
            if "OK\n" in decoded or "END\n" in decoded or "ERROR" in decoded or "WARNING" in decoded:
                break
        return decoded.strip()

    # ─────────────────────────────────────────────────────────────────
    # PUBLIC COMMANDS
    # ─────────────────────────────────────────────────────────────────

    def insert(self, doc_id: str, vector: list, metadata: dict = None) -> str:
        """
        Inserts a vector into the database.

        Wire format:
            INSERT id dims key=val key=val key=val f1 f2 ... fn\\n

        Args:
            doc_id:   unique identifier, max 32 chars, no spaces
            vector:   list of floats (e.g. 1536 floats from embedding)
            metadata: optional dict of up to 3 key=value pairs
                      e.g. {"source": "wiki", "chunk_id": "0"}

        Returns:
            "OK" on success, "ERROR ..." on failure
        """
        self._validate_id(doc_id)

        dims       = len(vector)
        floats_str = " ".join(str(f) for f in vector)

        if metadata:
            self._validate_metadata(metadata)
            meta_str = " ".join(f"{k}={v}" for k, v in metadata.items())
            message  = f"INSERT {doc_id} {dims} {meta_str} {floats_str}"
        else:
            message = f"INSERT {doc_id} {dims} {floats_str}"

        self._send(message)
        return self._receive()

    def query(self, vector: list, k: int = 3, filters: dict = None) -> list:
        """
        Searches the database for the k most similar vectors.

        Wire format:
            QUERY k dims key=val key=val f1 f2 ... fn\\n
            (filter goes AFTER dims, BEFORE floats — per protocol doc)

        Args:
            vector:  query vector (same dimensionality as inserted vectors)
            k:       number of results to return (default 3)
            filters: optional dict for metadata filtering
                     e.g. {"source": "wiki"} — only searches that subset

        Returns:
            list of (doc_id, score) tuples, sorted by score descending
        """
        dims       = len(vector)
        floats_str = " ".join(str(f) for f in vector)

        if filters:
            self._validate_metadata(filters)
            filter_str = " ".join(f"{k}={v}" for k, v in filters.items())
            message    = f"QUERY {k} {dims} {filter_str} {floats_str}"
        else:
            message = f"QUERY {k} {dims} {floats_str}"

        self._send(message)
        response = self._receive()

        # Parse "RESULTS n\n id score\n id score\n END\n" into list of tuples
        results = []
        for line in response.split("\n"):
            line = line.strip()
            if not line or line.startswith("RESULTS") or line == "END":
                continue
            parts = line.split()
            if len(parts) == 2:
                try:
                    results.append((parts[0], float(parts[1])))
                except ValueError:
                    continue   # skip malformed lines silently
        return results

    def delete(self, doc_id: str) -> str:
        """
        Deletes a vector from the database by ID.

        Wire format: DELETE id\\n
        Returns: "OK" or "ERROR id not found"
        """
        self._validate_id(doc_id)
        self._send(f"DELETE {doc_id}")
        return self._receive()

    def save(self) -> str:
        """
        Tells the server to persist its current state to disk.

        Wire format: SAVE\\n
        Returns: "OK"
        """
        self._send("SAVE")
        return self._receive()
