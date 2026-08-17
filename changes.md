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
    ->
    ->
    ->

5. Did not remove the ssot data from the .env file as python side still uses it, but engine is fully free from it and only reads .env for path and port

