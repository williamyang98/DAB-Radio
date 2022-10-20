cmake_minimum_required(VERSION 3.10)
project(easyloggingpp)

set(EASYLOGGINGPP_SRC_DIR ${CMAKE_SOURCE_DIR}/vendor/easyloggingpp/src)

add_library(easyloggingpp STATIC
    ${EASYLOGGINGPP_SRC_DIR}/easylogging++.cc)

target_include_directories(easyloggingpp PUBLIC ${EASYLOGGINGPP_SRC_DIR})
set_target_properties(easyloggingpp PROPERTIES 
    CXX_STANDARD 17)
