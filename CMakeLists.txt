cmake_minimum_required(VERSION 3.13)
project(VecDB_Server)

set(CMAKE_CXX_STANDARD 17)

#
include_directories(src)
include_directories(include)   
#   
add_executable(main src/main.cpp src/vector_store.cpp src/file_manager.cpp)

#   Test-Suite
include_directories(tests/integration)  
add_executable(test_vector_store tests/test_vector_store.cpp src/vector_store.cpp src/file_manager.cpp )
add_executable(test_file_manager tests/test_file_manager.cpp src/vector_store.cpp src/file_manager.cpp )