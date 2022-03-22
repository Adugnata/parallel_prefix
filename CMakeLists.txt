cmake_minimum_required(VERSION 3.15)
project(prefix)

set(CMAKE_CXX_STANDARD 14)

add_executable(prefix parallel_prefix.cpp)
target_link_libraries(prefix pthread)