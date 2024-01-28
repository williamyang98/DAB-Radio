cmake_minimum_required(VERSION 3.10)
project(basic_scraper)

option(BASIC_SCRAPER_USE_EASYLOGGING "Use easylogging for basic_scraper" OFF)

set(SRC_DIR ${CMAKE_CURRENT_LIST_DIR})
set(ROOT_DIR ${SRC_DIR}/..)

add_library(basic_scraper STATIC ${SRC_DIR}/basic_scraper.cpp)
set_target_properties(basic_scraper PROPERTIES CXX_STANDARD 17)
target_include_directories(basic_scraper PRIVATE ${SRC_DIR} ${ROOT_DIR})
target_link_libraries(basic_scraper PRIVATE basic_radio fmt)

if(BASIC_SCRAPER_USE_EASYLOGGING)
    target_link_libraries(basic_scraper PRIVATE easyloggingpp)
    target_compile_definitions(basic_scraper PRIVATE ELPP_THREAD_SAFE)
    target_compile_definitions(basic_scraper PUBLIC BASIC_SCRAPER_LOGGING_USE_EASYLOGGING)
endif()