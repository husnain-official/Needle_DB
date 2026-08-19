## Building Debugging tools:
### Quick-Overview of variables:
1.

### Rants:
1. What is id_length, kv_length, header_length ? What length, rename it all to size. ITS HOW MUCH SIZE THEY ARE USING IN MEMORY NOT LENGTH 
2. What is kv_lenght, kv_pair ? I am not going to get it unless i go and read the doxy that it is key-value
3. Why is dimensions in Header struct 4 bytes ? thats 4,294,967,295 (roughly 4.29 billion) but it will work fine for a 2byte version uint16_t which can hold 65,535.    (IMPORTANT)
4. In command_parser, and generally throughout the engine, i have stored DIMS_NO_OF_DIGITS, can someone tell me why that great idea came into my head, such a useless thing to add, when we are only concerened by the DIMS, WHY THE NO OF DIGITS ?
To make my point even concrete i am now adding text length in the insert protocol, for it i dont have to fix TEXT_LENGTH_NO_OF_DIGITS, i am genuinely curious as to what i was thinking while doing that.
5. Am i walking i a zoo ? SO much variety ? Pick one standard either use uintx_t by usecase everywhere or use int/size_t etc, why are you mixing them up everywhere when they can be easily be distinguished by use case.

### Problems Faced Booting up (WSL, ollama on windows): 
1. After the C++ build folder and starting the engine/server, if you follow readme line for line, it doesnt tell you to go back to root directory and run the python commands there. So, virtual enviournment is created inside the build folder.   
```
Even after following the lines of readme line for line, the server will show: couldn't read .env
DEBUG: and properly document.
BUG: Readme tells to run ./NeedleDB after building the server/C++ files inside the build function, but it will show error, as relative paths are used inside of C++ files, so we need to first come back to root using cd .. and then run build/NeedleDB and the same built files will run fine.
```
2. After you have started the C++ server, you will have to open a new terminal to start the application, add a reminder to activate the virtual enviornment in this terminal as well before starting the application.
3. NOTE: Add a reminder for the developers setup, to open the ollama app once or to check if it is already running in the background, else you will get a timeout error in your first ingestion attempt.

### Readme v2:
1. Be fully clear: I wanted to build the test files so i can update the code files with as much freedom as possible, and instantly know, where it fails, so i had to make test files, But i did not create test files, myself, I instructed claude to follow a pattern and give me prompt files, to give to GeminiProExtended, which will make the test files. So, I DID NOT WRITE THE TEST FILES, i only told ai to make them such that each point of failure(expected failure) can be tested easily.
2. 

## Questions: 
### About Internals:
1. What if i change all arrays to std::arrays ? is it a good decision or a bad one ?, even if arrays are rearely used mostly vectors are used.
2. 

### About Concepts:
1. Why binary files are much smaller than other file formats like .txt, .json etc
2. What is std::cerr, how is it different form std::cout


### Jobs:
### Command-Parser:
1. Take string command, parse it down, and fill all elements of a DB_entry/Vector/string, whatever is that parsing for.

