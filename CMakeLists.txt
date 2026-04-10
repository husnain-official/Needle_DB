cmake_minimum_required(VERSION 3.13)
project(VecDB_Server)

set(CMAKE_CXX_STANDARD 17)

#
include_directories(src)
include_directories(include)   
#   
add_executable(main src/main.cpp src/vector_store.cpp src/file_manager.cpp)
add_executable(main_test src/main_test.cpp src/vector_store.cpp src/file_manager.cpp tests/file_test.cpp)
#add_executable(file_test src/file_manager.cpp )
#
#target_include_directories(main PRIVATE include src/vector_store.h)
#target_include_directories(main_test PRIVATE include tests/file_test.cpp)