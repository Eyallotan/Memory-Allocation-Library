cmake_minimum_required(VERSION 3.10)
project(OS_2)

set(CMAKE_CXX_STANDARD 11)

add_executable(OS_2 malloc_4.cpp)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -Wall -o main")