cmake_minimum_required(VERSION 3.13)
project(NeedleDB_Server)

set(CMAKE_CXX_STANDARD 17)

#
include_directories(src)
include_directories(include)   
#   
add_executable(NeedleDB src/main.cpp src/vector_store.cpp src/file_manager.cpp src/vector_server.cpp src/command_parser.cpp src/ivf.cpp)

#   Test-Suite
option(BUILD_TESTS "Build test executables" OFF)
if(BUILD_TESTS)
    add_executable(test_vector_store tests/test_vector_store.cpp src/vector_store.cpp src/file_manager.cpp src/ivf.cpp)
    add_executable(test_file_manager tests/test_file_manager.cpp src/vector_store.cpp src/file_manager.cpp src/ivf.cpp)
    add_executable(test_vector_server tests/test_vector_server.cpp src/vector_store.cpp src/file_manager.cpp src/vector_server.cpp src/command_parser.cpp src/ivf.cpp)
endif()

# Link socket libraries (required on some Linux distros)
target_link_libraries(NeedleDB pthread)