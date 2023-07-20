cmake_minimum_required(VERSION 3.10)
project(fmtlib)

set(FMT_SRC_DIR ${CMAKE_CURRENT_LIST_DIR}/../vendor/fmt)
add_subdirectory(${FMT_SRC_DIR})
