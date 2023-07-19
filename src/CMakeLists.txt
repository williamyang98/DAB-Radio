cmake_minimum_required(VERSION 3.10)
project(dab_ofdm_modules)

set(SRC_DIR ${CMAKE_CURRENT_LIST_DIR})
add_subdirectory(${SRC_DIR}/dab)
add_subdirectory(${SRC_DIR}/ofdm)
add_subdirectory(${SRC_DIR}/basic_radio)
add_subdirectory(${SRC_DIR}/basic_scraper)
