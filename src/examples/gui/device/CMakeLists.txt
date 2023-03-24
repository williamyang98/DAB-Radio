cmake_minimum_required(VERSION 3.10)

set(SRC_DIR ${CMAKE_CURRENT_LIST_DIR})
set(ROOT_DIR ${CMAKE_SOURCE_DIR}/src)
set(EXAMPLES_DIR ${CMAKE_SOURCE_DIR}/src/examples)

add_library(device_gui STATIC ${SRC_DIR}/render_device_selector.cpp)
set_target_properties(device_gui PROPERTIES CXX_STANDARD 17)
target_include_directories(device_gui PRIVATE ${SRC_DIR} ${ROOT_DIR} ${EXAMPLES_DIR})
target_link_libraries(device_gui PRIVATE imgui device_lib fmt)