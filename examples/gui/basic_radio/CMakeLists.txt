cmake_minimum_required(VERSION 3.10)

set(SRC_DIR ${CMAKE_CURRENT_LIST_DIR})
set(EXAMPLES_DIR ${SRC_DIR}/../..)
set(ROOT_DIR ${CMAKE_SOURCE_DIR}/src)

add_library(basic_radio_gui STATIC
    ${SRC_DIR}/render_basic_radio.cpp
    ${SRC_DIR}/render_common.cpp
    ${SRC_DIR}/basic_radio_view_controller.cpp
    ${SRC_DIR}/texture.cpp
    ${SRC_DIR}/formatters.cpp)
set_target_properties(basic_radio_gui PROPERTIES CXX_STANDARD 17)
target_include_directories(basic_radio_gui PRIVATE ${SRC_DIR} ${ROOT_DIR} ${EXAMPLES_DIR})
target_include_directories(basic_radio_gui PRIVATE ${CMAKE_SOURCE_DIR}/vendor/stb ${SRC_DIR}/../)
target_link_libraries(basic_radio_gui PRIVATE basic_radio imgui fmt)