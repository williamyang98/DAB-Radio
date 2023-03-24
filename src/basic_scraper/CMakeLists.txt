cmake_minimum_required(VERSION 3.10)

set(SRC_DIR ${CMAKE_CURRENT_LIST_DIR})
set(ROOT_DIR ${CMAKE_SOURCE_DIR}/src)

add_library(basic_scraper STATIC ${SRC_DIR}/basic_scraper.cpp)
set_target_properties(basic_scraper PROPERTIES CXX_STANDARD 17)
target_include_directories(basic_scraper PRIVATE ${SRC_DIR} ${ROOT_DIR})
target_link_libraries(basic_scraper PRIVATE basic_radio easyloggingpp fmt)
target_compile_definitions(basic_scraper PRIVATE ELPP_THREAD_SAFE)

if (MSVC)
target_compile_options(basic_scraper PRIVATE "/MP")
endif()