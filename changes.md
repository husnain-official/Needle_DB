### Mult-threading of handle_clients()
1. First i changed the code to run and pass all test files with 1 sub-thread for the only client connected, this was simple enough and was done using the thread library, and simple member functions like detach to detach the sub-thread from the parent thread, so the clients may remain independent which was the entire purpose. 
2. NOTE: OK/n is now being switch to more usefull outputs like "DELETE <Successful>\n"
3. Updated server test files to also test the concurrency, and multiple clients, Passed. 

### Cleaning up- before deciding what to do next.
1. Updated the Config struct to only have members: port, entry_db_path, text_db_path
2. Moved the Config struct to schema.hpp as it also provides system-level constraints.
3. Config struct's entry file now renamed to "./data/database_entry.vdb" from "./data/database.vdb"
4. Cleaned up schema.hpp, needs new doxy for 'Vector'. 
5. Cleaned up similarities.hpp, has dead code (2 cosine_similarity functions) in it. 
6. Cleaned up env_config.hpp, NOTE: in the final documentation, EXPLICITLY, state the code in this file was AI generated.
7. Cleaned up command_parser.h/cpp and updated the doxy in .h manually.
8. File_manager.delete_entry() parameter index is now a const
9. Cleaned up file_manager.h/cpp
10. 

### Persistence of ivf centroids(next commit, not this one)



NOTE:1  |   Types.hpp cleaned, now only has 2 structs(Query_result, Parse_result), and one conversion function which copies data from a 'Entry' struct to a 'Vector' struct

NOTE:2  |   'Vector' struct in schema.hpp has no doxy. 

NOTE:3  |   All files use #ifndef and #endif, blocks but schema.hpp uses #prama once, switch all to 1. 

NOTE:4  |   in similarities.hpp two cosine_similarity() functions exist, they are not used anywhere in the system, only present because of their initial use from before release of v1. Can safely delete both functions, or just let them be. They are not written efficiently either, both use sqrt frequently, and disobey the "dont repeat yourself" rule.

NOTE:5  |   In file_manager.h, read_text() needs doxy.

NOTE:6  |   File_manager.find_by_id() is a O(n) function as the database is not sorted, make this a goal for v3, to somehow get this logrithmic, ofcourse after some sort of linearithmic sorting.

NOTE:7  |   File_manager.compact(), also needs stored safety conditions, maybe v2 or v3.
NOTE:8  |   Vector_server needs new doxy
NOTE:9  |  














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