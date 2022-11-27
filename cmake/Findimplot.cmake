cmake_minimum_required(VERSION 3.16)
project(implot)

set(SRC_DIR ${CMAKE_SOURCE_DIR}/vendor/implot)

add_library(implot STATIC
    "${SRC_DIR}/implot.h"
    "${SRC_DIR}/implot.cpp"
    "${SRC_DIR}/implot_internal.h"
    "${SRC_DIR}/implot_items.cpp")

target_include_directories(implot PUBLIC ${SRC_DIR})
target_link_libraries(implot PUBLIC imgui)