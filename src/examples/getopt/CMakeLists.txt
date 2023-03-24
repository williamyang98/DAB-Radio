cmake_minimum_required(VERSION 3.10)

set(SRC_DIR ${CMAKE_CURRENT_LIST_DIR})
set(ROOT_DIR ${CMAKE_SOURCE_DIR}/src)

add_library(getopt STATIC ${SRC_DIR}/getopt.c)
target_include_directories(getopt PRIVATE ${SRC_DIR})