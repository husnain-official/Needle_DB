cmake_minimum_required(VERSION 3.13)
project(VecDB_Server)

set(CMAKE_CXX_STANDARD 17)

#
include_directories(src)
#   
add_executable(main src/main.cpp src/vector_store.cpp)
add_executable(main_test src/main_test.cpp src/vector_store.cpp)

include_directories(include)   
target_include_directories(main PRIVATE include src/vector_store.h)