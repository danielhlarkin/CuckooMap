cmake_minimum_required(VERSION 3)
project(CuckooMap)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 11)

add_library(cuckoo INTERFACE)
target_include_directories(cuckoo INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")
