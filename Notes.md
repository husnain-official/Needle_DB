## Building Debugging tools:
### Quick-Overview of variables:
1. The variables used throughout the engine are stored in `env_config.hpp` and validated to confirm in main.cpp at bootup. 
2. Header is limited to file_manager.h/.cpp
3. Entry is also in file_manager.h/.cpp
```
Entry:  Flag(1-Byte), Id-size(32-Byte), Metadata[pairs(3-Bytes) * key-value-size(32+32 = 64-Bytes)], Embeddings     [dimenstions(1024-Bytes) * size_of_float(8-Bytes)].  
    Total length = 1 + 32 + (64*3) + (8*1024).

Note:   file_manager.cpp has a element named record_size representing the overall size of entry.     
```

```
Header: A struct limited only to file_manager.cpp, representing the header and its elements. 
Size:   live_vector (8-Bytes), dead_vector(8-Bytes), dimensions(4-Bytes), magic-number(4-Bytes), version(1-Byte), Id-size(1-Byte), keys/values-size(1-Byte), max_kv(1-Byte), padding(4-Byte)
    Total = 1+8+8+4+4+1+1+1+1+4 = 32-Bytes.

Note: Vector-struct is identicle to the structure of a entry, but file_manger.h/cpp does not use vector for it but uses, individual elements passed through as parameters. 
```
### Problems Faced Booting up (WSL, ollama on windows):
1. After the C++ build folder and starting the engine/server, if you follow readme line for line, it doesnt tell you to go back to root directory and run the python commands there. So, virtual enviournment is created inside the build folder.   
```
Even after following the lines of readme line for line, the server will show: couldn't read .env
DEBUG: and properly document.
BUG: Readme tells to run ./NeedleDB after building the server/C++ files inside the build function, but it will show error, as relative paths are used inside of C++ files, so we need to first come back to root using cd .. and then run build/NeedleDB and the same built files will run fine.
```
2. After you have started the C++ server, you will have to open a new terminal to start the application, add a reminder to activate the virtual enviornment in this terminal as well before starting the application.
3. NOTE: Add a reminder for the developers setup, to open the ollama app once or to check if it is already running in the background, else you will get a timeout error in your first ingestion attempt.
