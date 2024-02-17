cmake_minimum_required(VERSION 3.10)
project(basic_radio)

option(BASIC_RADIO_USE_EASYLOGGING "Use easylogging for basic_radio" OFF)

set(SRC_DIR ${CMAKE_CURRENT_LIST_DIR})
set(ROOT_DIR ${SRC_DIR}/..)

add_library(basic_radio STATIC
    ${SRC_DIR}/basic_radio.cpp
    ${SRC_DIR}/basic_fic_runner.cpp
    ${SRC_DIR}/basic_audio_controls.cpp
    ${SRC_DIR}/basic_audio_channel.cpp
    ${SRC_DIR}/basic_dab_plus_channel.cpp
    ${SRC_DIR}/basic_dab_channel.cpp
    ${SRC_DIR}/basic_data_packet_channel.cpp
    ${SRC_DIR}/basic_slideshow.cpp)
set_target_properties(basic_radio PROPERTIES CXX_STANDARD 17)
target_include_directories(basic_radio PRIVATE ${SRC_DIR} ${ROOT_DIR})
target_link_libraries(basic_radio PRIVATE dab_core fmt)

if(BASIC_RADIO_USE_EASYLOGGING)
    target_link_libraries(basic_radio PRIVATE easyloggingpp)
    target_compile_definitions(basic_radio PRIVATE ELPP_THREAD_SAFE)
    target_compile_definitions(basic_radio PUBLIC BASIC_RADIO_LOGGING_USE_EASYLOGGING)
endif()