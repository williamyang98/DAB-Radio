cmake_minimum_required(VERSION 3.10)

set(SRC_DIR ${CMAKE_CURRENT_LIST_DIR})
set(ROOT_DIR ${CMAKE_SOURCE_DIR}/src)
set(EXAMPLES_DIR ${CMAKE_SOURCE_DIR}/src/examples)

add_library(profiler_gui STATIC ${SRC_DIR}/render_profiler.cpp)
set_target_properties(profiler_gui PROPERTIES CXX_STANDARD 17)
target_include_directories(profiler_gui PRIVATE ${SRC_DIR} ${ROOT_DIR} ${EXAMPLES_DIR})
target_link_libraries(profiler_gui PRIVATE imgui)