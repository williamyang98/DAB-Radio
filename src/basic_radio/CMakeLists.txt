cmake_minimum_required(VERSION 3.10)

set(SRC_DIR ${CMAKE_CURRENT_LIST_DIR})
set(ROOT_DIR ${CMAKE_SOURCE_DIR}/src)

add_library(basic_radio STATIC
    ${SRC_DIR}/basic_radio.cpp
    ${SRC_DIR}/basic_fic_runner.cpp
    ${SRC_DIR}/basic_dab_plus_channel.cpp
    ${SRC_DIR}/basic_slideshow.cpp
    ${SRC_DIR}/basic_database_manager.cpp)
set_target_properties(basic_radio PROPERTIES CXX_STANDARD 17)
target_include_directories(basic_radio PRIVATE ${SRC_DIR} ${ROOT_DIR})
target_link_libraries(basic_radio PRIVATE dab_core easyloggingpp fmt)
target_compile_definitions(basic_radio PRIVATE ELPP_THREAD_SAFE)