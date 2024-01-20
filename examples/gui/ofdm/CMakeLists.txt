cmake_minimum_required(VERSION 3.10)

set(SRC_DIR ${CMAKE_CURRENT_LIST_DIR})
set(EXAMPLES_DIR ${SRC_DIR}/../..)
set(ROOT_DIR ${CMAKE_SOURCE_DIR}/src)

add_library(ofdm_gui STATIC ${SRC_DIR}/render_ofdm_demod.cpp ${SRC_DIR}/render_profiler.cpp)
set_target_properties(ofdm_gui PROPERTIES CXX_STANDARD 17)
target_include_directories(ofdm_gui PRIVATE ${SRC_DIR} ${ROOT_DIR} ${EXAMPLES_DIR})
target_link_libraries(ofdm_gui PRIVATE imgui implot)