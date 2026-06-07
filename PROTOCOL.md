# TCP Protocol Specification
## Overview

Communication uses a plain-text line protocol over TCP.
- Every **command** ends with `\n`
- Every **response** ends with `\n`
- Server processes one command at a time 

---

## Commands

### INSERT
Stores a vector with optional metadata labels.

```
INSERT <id> <dims> [key=val ...] f1 f2 f3 ... fn\n
```

| Field | Type | Limit | Description |
|-------|------|-------|-------------|
| `id` | string | max 32 chars, no spaces | unique document identifier |
| `dims` | integer | — | number of float values that follow |
| `key=val` | string pairs | max 3 pairs, keys/values max 32 chars, no spaces | optional metadata |
| `f1...fn` | float | `dims` values | the embedding vector |

**Response:** `OK\n`

**Example:**
```
INSERT kb_ai_ml_chunk_0 1024 source=kb_ai_ml chunk_id=0 0.123 0.456 ...\n
```

---

### QUERY
Finds the k most similar vectors to the query vector.

```
QUERY <k> <dims> [key=val ...] f1 f2 f3 ... fn\n
```

| Field | Type | Description |
|-------|------|-------------|
| `k` | integer | number of results to return |
| `dims` | integer | number of float values that follow |
| `key=val` | string pairs | optional metadata filter — only search matching vectors |
| `f1...fn` | float | the query vector |

**Note:** filter comes **after dims, before floats**.

**Response:**
```
RESULTS <k>\n
<id> <score>\n
<id> <score>\n
...
END\n
```

**Example:**
```
QUERY 3 1024 source=kb_ai_ml 0.123 0.456 ...\n
```

---

### DELETE
Removes a vector from the database.

```
DELETE <id>\n
```

**Response:** `OK\n` or `ERROR id not found\n`

---

### SAVE
Persists the current database state to disk.

```
SAVE\n
```

**Response:** `OK\n`

---

## Protocol Rules

1. Commands do **not** include a trailing space before `\n`
2. Float values use Python's default `str(float)` formatting
3. Server responses are always terminated by `\n`
4. A response of `OK` means the command succeeded
5. A response starting with `ERROR` means it failed — the rest of the line explains why
6. Connection stays open between commands — no reconnect needed per command

---

## Limits

| Item | Limit |
|------|-------|
| ID length | max 32 characters |
| ID characters | no spaces |
| Metadata pairs per command | max 3 |
| Metadata key length | max 32 characters |
| Metadata value length | max 32 characters |
| Metadata key/value | no spaces allowed |
| Vector dimensions | 1024 (mxbai-embed-large) |
