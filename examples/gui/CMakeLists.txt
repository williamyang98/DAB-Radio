cmake_minimum_required(VERSION 3.10)
project(examples_gui)

set(SRC_DIR ${CMAKE_CURRENT_LIST_DIR})

add_subdirectory(${SRC_DIR}/audio)
add_subdirectory(${SRC_DIR}/basic_radio)
add_subdirectory(${SRC_DIR}/device)
add_subdirectory(${SRC_DIR}/ofdm)