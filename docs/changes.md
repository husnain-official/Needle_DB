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

### Persistence of ivf centroids
1. 
* **Fixed critical truncation bug:** Prevented accidental wiping of existing database files in the `File_manager` constructor when only a single file (like the index) was missing.
* **Decoupled index file logic:** The index file is now treated as a recoverable, secondary asset that will silently recreate itself if missing, preserving primary data files.
* **Added strict corruption safeguards:** Added an exclusive-OR check (`entry_exists != text_exists`) to throw a runtime error if the core database files are in a partial or corrupted state.
2. 
* **Added index state detection:** Implemented `is_index_populated()` in `File_manager` using `std::filesystem::is_empty` to detect 0-byte (uninitialized) index files.
* **Integrated auto-rebuild on boot:** Updated server initialization logic to dynamically route between building a new IVF index from the vector store or loading pre-calculated centroids from disk based on the index file's size.
3. 
* **Centralized hardcoded schema variables:** Extracted `MAX_CENTROIDS` and `MAX_PROBES_SEARCH` into the `schema` namespace and updated the `IVF_index` constructor to use them as default parameters.
* **Implemented index deserialization:** Added `populate_index_` in `File_manager` to read centroid data directly from the binary file into memory.
* **Integrated fast-load server routing:** Updated the server boot sequence to calculate required centroid dimensions and load existing indices directly from disk into `IVF_index` using move semantics.
4. 
* **Added index persistence:** Implemented `write_index_` and `read_index_` in `File_manager` for saving and loading the calculated IVF centroids to/from the binary index file.
* **Integrated list rebuilding:** Added `build_lists()` in `IVF_index` to re-assign existing vector embeddings to the loaded centroids on server boot-up.
* **Added safe memory management:** Implemented strict `sizeof(float)` byte calculations for binary I/O operations and ensured the inverted `lists` array is safely resized prior to index rebuilding.
5. 
made it such, if the index files size is 0, the server will recreate the indexes, and save them.
6. 
Updated vector_server tests, file_manager tests

> **Note on AI Generation:** All of the following text is written by AI to maintain a compact and detailed format. If you want to read the original, non-AI wording and follow the author's exact thought process, please revert to the commit made on 22/9/26.

### New Commands & Mechanics (8/9/26)
* **New `OPTIMIZE` Command:** Added parsing logic to `command_parser.h/.cpp` and an execution handling block in `vector_server.cpp`.
* **Auto-Compaction:** Updated the `DELETE` command to conditionally trigger `compact()`. Return messages are now dynamic based on compaction success (e.g., `DELETE <Successful>, Compaction<Successful>\n` or `WARNING <Database compaction failed>.\n`).
* **Optimization Suggestion System:** The server now tracks the entry count at the time of the last `build_` execution. 
* Implemented two new functions in `file_manager` to read and write this centroid build state, slightly modifying the database file schema.
* **Dynamic Client Warnings:** If current database entries double the amount recorded at the last optimization, the server appends a warning to client responses (e.g., `INSERT <Successful>, WARNING<OPTIMIZE better for needed searches.>\n`).
* **Known Architecture Flaw:** The current `OPTIMIZE` execution locks all connected clients behind a mutex until completion. Documented as a critical system flaw to be resolved in v3.

### Server Optimization & IVF Rebuild
> **Note on AI Generation:** The conceptual logic for the fixed-size sampling was understood and reviewed manually, but the code implementation for `ivf.build_()` was generated by AI.
* Debugged and resolved a persistent bug in the vector-server test suite.
* Upgraded the `build_` function to compute centroids against a fixed-size sample of vectors rather than the entire dataset, drastically improving optimization times.

### Code Cleanup & Doxygen
> **Note on AI Generation:** Doxygen comments for the cleaned files were generated using AI.
* Completed code review, cleanup, and Doxygen documentation generation for the following core files: `schema.hpp`, `env_config.hpp`, `types.hpp`, `command_parser.h/.cpp`, `file_manager.h/.cpp`, `vector_server.h/.cpp`, and `vector_store.h/.cpp`.
* Verified zero regressions post-cleanup via the test suite.

### Project Restructuring & Documentation
* Relocated `src`, `include`, `tests`, `Dockerfile`, and `CMakeLists.txt` into a new structured project directory (internal path links to be updated accordingly).
* Deleted the obsolete `Protocol.md` (which contained stale v1 data).
* Generated a comprehensive `ENGINE.md` that consolidates all protocol info, commands, schemas, and internal architectural decisions.
* Identified final deliverables for v2: `README.md` (pending Python completion), `ENGINE.md`, and a consolidated `Changes/Notes.md`.

### Engine Updates & Bug Fixes
* Updated directory paths across `engine/schema.hpp`, `file_manager.cpp` (specifically the temporary path in `compact()`), and `file_manager_tests.cpp`.
* Fixed a return type mismatch bug in the `find_by_id(const std::string &id)` function signature.
* Updated `CMakeLists.txt` to reflect the new file structure.
* Verified engine stability: Built all executables and successfully passed all integrated test suites.

> ----------------------------------------------------------------------------------------------------------------------
### Python Backend & SSOT
> **Note on AI Generation:** The Python backend development was split 50/50 with AI.
* Created a dedicated schema file for the Python backend and centralized all constant sources into it.
* Updated `searcher.py` and `embedder.py` to integrate the new configuration.
* Implemented `schema_loader.py` to maintain the Single Source of Truth (SSOT). It uses regular expressions to parse `schema.hpp`, isolate the `schema` namespace, and dynamically extract `constexpr` values.
* **Note:** This implementation forces a runtime read on system startup to supply the Python module with necessary engine variables.

### To-Dos & Future Iterations
* Update the knowledge base path in `ingerster.py`.
* Implement a Python-side tracking file to persist which files have been ingested upon system boot (the engine will not manage this state).
* Fix state persistence: currently, ingested chunk statistics are erased when a client disconnects or the chat is cleared.
* Update `.env.example` to reflect recent path and port changes made to `.env`.
* Add a troubleshooting section in `README.md` detailing steps for missing or failing Linux TCP server files.
*

* For now, the file in whcich the files which have been ingested will be saved will just be a simple txt file, or a csv file, later on in future IF EVER i find some time, alot of fun things can be done by switching to csv format, how many chunks each file is, how many top results were from this file, etc etc. 

### V3: 
- A Command like STATS, to which the engine returns all the stats of what it has, how many entries it has stored et. 
- Note: How can we delete a whole file from the engine ? currently we can not, but in the futute how ? simple we need to store which entry # the file started from and from where it lasted/how many entires/chunks was it. So, later on we can make a new command DELETE_FILE FILE_NAME, this will take the file name, pyton side will use it to get the entry # from where it started and where i ended, and send those numbers to the engine, the engine will recreate the database and delete all entires between those numbers. or just soft delete them, but this can be done very easily, i did not think of this at all till now, but a very good thing for v3. 
- 

### 9/30/26 (1st commit):
1. Now files saved are stored and also their additional info, fully/partial/failed ingestion of the file, number of chunks etc. 
2. The stats are also saved. 
3. Made a new file in client/pipeline/ledger.py which generates a new file in the client/data folder and saves data there. 
4. Client side data is all to be stored in clinet/data, also the knowledge base is now client/data/knowledge_base/
5. Updated: Ingestor.py, rag_chatbot.py, app_gui.py.
6. Delted the services/engine/data/documents folder and replaced it with the new knowledgebase folder in above point mentioned. 
7. NOTE: Idiot :), the chunk-words = 150 and max-text-length = 999 have no relation in the code, but they are 2 things conditioning the same text, if a 150 word chunk is passed to engine it will reject it, why ? because my intelligent brain decided there was no relation between these 2 variables, so right now i am only doing a patch up and ramping up the max-text-length to like 1200 to accomodate the text and a few multiple byte chars as well, but this is not a fix, this is only just a patch in real terms.
150 chars = 1090 bytes, but i am setting it to 1200, to also allow multi bytes characters.
i need to rember even if they only teach us ASCII the world has moved on from that decades ago, new standards are used now a days, just something to keep in mind for next time.
8. Ran the app and tested it. 

### 9/30/26 (2st commit):
1. Updated 1200 max text lenght in engine.md
2. 