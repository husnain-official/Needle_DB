## Changes-Made:
### SSOT for Schema(Header & Entry):
First seperate the source of schema from the source code, so we can start making the tests.
1. Removed Header from file_manager.h
2. Removed meta_data form types.h
3. In file_manager's constructor:
    previously: we passed the schema through parameters
    now: the header is build with data directly from the namespace schema
    what this means: 
        1. We dont have to manually send parameters, 
        2. We dont have to set values to the header, but a file_manager creates its own header, and after construction it already has correct data in it.
4. Got updated:
    -> File_manager()
    -> Deleted:     env_config.hpp 
    -> Removed config instances dependency from each file and shifted to direct use of constexprs from schema.hpp
5. Did not remove the ssot data from the .env file as python side still uses it, but engine is fully free from it and only reads .env for path and port

### 2nd:
1. Updated the command_parser to the new command formats.
2. Moved 'Vector' struct form types.hpp to schema.hpp
1. **COMMAND-PARSER**:  (Either change or document)
- Why does insert does not check for duplicates equal signs in key=value while query does ? (Fixed.)
- Why does SAVE/LOAD allow S A V E and L O A D.     (Fixed.)

### 3rd:
1. New Rule to be followed: Vector_store and any other files which are ram only will use fully automated structures like string and vectors.
2. New Rule: Only those will use raw arrays, c-strings, POD, which are to be fully turned into binary and stored in one go, Like DB_header, DB_entry.
3. Update: 
- Replaced old write_entry() with new version, which dumps the entire struct in one go instead of validating/padding and doing things that are not its responsibility.
- Replaced old read_entry() with new version, now reads the flag first and returns if dead else reads the reast in 1 go.
- file_manager.contract(), written down, now can contract database down to only live vectors after deletion of dead ones.
- IMPORTANT: File_manager, currently writes the text in 1 entry as well padded upto 999btyes, the original plan was to put the text in another file, not in the same one, CHANGE it.



### Unexpted Behavior/TO-be-Changed:
1. **COMMAND-PARSER**:  (Either change or document)
- 

