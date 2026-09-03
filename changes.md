














> **Note on AI Generation:** All of the following text is written by AI to maintain a compact and detailed format. If you want to read the original, non-AI wording and follow the author's exact thought process, please revert to the commit made on 23/8/26.

### SSOT for Schema (Header & Entry)
* A Single Source of Truth (SSOT) was established for the schema by separating it from the source code, enabling easier testing.
* The `Header` and `meta_data` were removed from `file_manager.h` and `types.h`, respectively.
* The `file_manager` constructor now automatically builds its own header using data directly from the schema namespace instead of relying on manually passed parameters.
* The `env_config.hpp` file was deleted. The engine no longer relies on config instances and instead directly uses `constexprs` from `schema.hpp`.
* The `.env` file is now only read for the path and port by the engine, though the Python side still relies on the SSOT data there.

### Command-Parser
* The command parser was updated to handle new command formats.
* The `Vector` struct was relocated from `types.hpp` to `schema.hpp`.
* Fixed a bug where `insert` did not check for duplicate equal signs in key=value pairs (unlike `query`).
* Fixed an issue where `SAVE` and `LOAD` incorrectly allowed spaced-out characters (e.g., S A V E).

### Schema, File-Manager
* **New Rule:** RAM-only files (like `Vector_store`) must use automated structures like vectors and strings.
* **New Rule:** Binary storage components must use raw arrays, c-strings, and PODs to allow for one-go memory dumping.
* `write_entry()` was replaced with a new version that dumps the entire struct at once, removing validation and padding logic that falls outside its responsibility.
* `read_entry()` was updated to read the status flag first, aborting if the entry is dead, or reading the rest in one go if alive.
* The `contract()` function was implemented to compress the database down to only live vectors by removing dead ones.
* Identified an architectural flaw to be changed: text was originally being written into the same entry file and padded up to 999 bytes, which will be separated.

### File-Manager (Dynamic Database)
* Fixed-length data and dynamic-length text are now separated into two different files. While this adds complexity and disk reads, it was deliberately chosen to practice implementing dynamic database internals.
* Text elements and sizes were moved from `DB_entry` to a new `DB_text_entry`.
* All core functions (`write_entry`, `read_entry`, `delete_entry`, `compact`) were updated to support both files.
* The dedicated text file operates without a header using a strict `<flag><text>` schema.
* If one or both database files are missing, the app will silently create new ones and destroy previous data.
* A test file for `file_manager.h/.cpp` was added.

### 5th (Commit)
* Logic was updated across `INSERT`, `DELETE`, `SAVE`, and `LOAD` to match the new file structure (with `QUERY` remaining).
* In `vector_store::normalise_vector()`, division was swapped for multiplication, and safeguards against infinite or NaN values were introduced.
* **Reminder:** The indices of the database and the RAM `vector_store` are currently not parallel.
* Getter functions in the `vector_store` class received const correctness, and dead code was removed.
* Function parameters in `ivf.cpp` were updated.
* Added two parallel arrays (`text_lengths` and `text_offsets`) to `vector_store` for O(1) text reads during server queries.
* The `QUERY` format was updated to output as `<id> <similarity> <text>\n`.
* Added `read_text()` in `file_manager()` to extract text in O(1) time using text lengths and offsets.
* The entry parameter in `server.write_entry()` was made non-const so the system can acquire and store the `text_offset` in RAM.

### 6th (Commit)
* Full integration and unit test files have been created to test each file and their integrations. 
* Fixed a bug in the `file_manager.read_text()` condition regarding when to return an error.
* **Note:** Author takes zero credit for writing the code in the test files, noting they were generated using AI based on provided failure points.

### Unexpected Behavior / To-Be-Changed
* **File-Manager (Compact):** The `compact()` function might corrupt files if the system shuts down mid-execution.
* **File-Manager (Data Wipe):** The constructor currently destroys working data and silently starts over if either database file is deleted. This needs to be documented and eventually changed to throw a runtime error instead.