cmake_minimum_required(VERSION 3.10)
project(easyloggingpp)

set(EASYLOGGINGPP_SRC_DIR ${CMAKE_SOURCE_DIR}/vendor/easyloggingpp/src)

add_library(easyloggingpp STATIC
    ${EASYLOGGINGPP_SRC_DIR}/easylogging++.cc)
target_include_directories(easyloggingpp PUBLIC ${EASYLOGGINGPP_SRC_DIR})
set_target_properties(easyloggingpp PROPERTIES CXX_STANDARD 17)
target_compile_definitions(easyloggingpp PRIVATE ELPP_THREAD_SAFE)

# NOTE: You must have the ELPP_THREAD_SAFE defined in all other sources that include this library
#       Otherwise you will end up using the single threaded data structures erroneously
#       If the initialisation macro is configured with the thread safe define 
#       than these single threaded data structures will be uninitialised
#       and you will get a runtime segfault on a NULL or garbage pointer
