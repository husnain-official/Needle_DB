cmake_minimum_required(VERSION 3.13)
project(NeedleDB_Server)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# 1.Include the FetchContent module
include(FetchContent)
# 2. Declare the GoogleTest GitHub repository
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        v1.14.0
)
# 3. Download and make the GoogleTest targets available
FetchContent_MakeAvailable(googletest)

# FIXED: Paths are now relative to services/engine/
include_directories(include)
include_directories(src)   
include_directories(tests) 

# Main Executable
add_executable(NeedleDB src/main.cpp src/vector_store.cpp src/file_manager.cpp src/vector_server.cpp src/command_parser.cpp src/ivf.cpp)

# 4. Create test executables
add_executable(command_parser_tests tests/command_parser_tests.cpp src/command_parser.cpp)
add_executable(file_manager_tests tests/file_manager_tests.cpp src/file_manager.cpp)
add_executable(vector_store_tests tests/vector_store_tests.cpp src/vector_store.cpp src/ivf.cpp)
add_executable(ivf_tests tests/ivf_tests.cpp src/ivf.cpp src/vector_store.cpp)
add_executable(vector_store_ivf_integration_tests tests/vector_store_ivf_integration_tests.cpp src/vector_store.cpp src/ivf.cpp)
add_executable(vector_server_tests tests/vector_server_tests.cpp src/vector_server.cpp src/command_parser.cpp src/file_manager.cpp src/vector_store.cpp src/ivf.cpp)

# 5. Link GoogleTest to your executables
target_link_libraries(command_parser_tests PRIVATE GTest::gtest_main)
target_link_libraries(file_manager_tests PRIVATE GTest::gtest_main)
target_link_libraries(vector_store_tests PRIVATE GTest::gtest_main)
target_link_libraries(ivf_tests PRIVATE GTest::gtest_main)
target_link_libraries(vector_store_ivf_integration_tests PRIVATE GTest::gtest_main)
target_link_libraries(vector_server_tests PRIVATE GTest::gtest_main)

# Link socket libraries
target_link_libraries(NeedleDB pthread)