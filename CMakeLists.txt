cmake_minimum_required(VERSION 3.13)
project(NeedleDB_Server)

set(CMAKE_CXX_STANDARD 17)


# 1.Include the FetchContent module
include(FetchContent)
# 2. Declare the GoogleTest GitHub repository and the version you want
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        v1.14.0 # It is highly recommended to pin a specific version tag
)
# 3. Download and make the GoogleTest targets available to your project
FetchContent_MakeAvailable(googletest)


#
include_directories(src)
include_directories(include)   
#   
add_executable(NeedleDB src/main.cpp src/vector_store.cpp src/file_manager.cpp src/vector_server.cpp src/command_parser.cpp src/ivf.cpp)

#   Test-Suite
# option(BUILD_TESTS "Build test executables" OFF)
# if(BUILD_TESTS)
#     add_executable(test_vector_store tests/test_vector_store.cpp src/vector_store.cpp src/file_manager.cpp src/ivf.cpp)
#     add_executable(test_file_manager tests/test_file_manager.cpp src/vector_store.cpp src/file_manager.cpp src/ivf.cpp)
#     add_executable(test_vector_server tests/test_vector_server.cpp src/vector_store.cpp src/file_manager.cpp src/vector_server.cpp src/command_parser.cpp src/ivf.cpp)
# endif()
# 4. Create test executable
add_executable(command_parser_tests tests/command_parser_tests.cpp src/command_parser.cpp)
add_executable(file_manager_tests tests/file_manager_tests.cpp src/file_manager.cpp)

# 5. Link GoogleTest to your executable
# Using GTest::gtest_main automatically provides a main() function for your tests
target_link_libraries(command_parser_tests PRIVATE GTest::gtest_main)
target_link_libraries(file_manager_tests PRIVATE GTest::gtest_main)

# Link socket libraries (required on some Linux distros)
target_link_libraries(NeedleDB pthread)