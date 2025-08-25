cmake_minimum_required(VERSION 3.16)
project(mpg123)

set(SRC_DIR ${CMAKE_CURRENT_LIST_DIR}/../vendor/mpg123)

set(BUILD_LIBOUT123 OFF CACHE BOOL "build libout123" FORCE)
add_subdirectory(${SRC_DIR}/ports/cmake)

add_library(mpg123 INTERFACE)
target_link_libraries(mpg123 INTERFACE libmpg123)
target_include_directories(mpg123 INTERFACE ${SRC_DIR}/src/include)

