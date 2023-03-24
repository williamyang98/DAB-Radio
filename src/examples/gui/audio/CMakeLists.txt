cmake_minimum_required(VERSION 3.10)

set(SRC_DIR ${CMAKE_CURRENT_LIST_DIR})
set(ROOT_DIR ${CMAKE_SOURCE_DIR}/src)
set(EXAMPLES_DIR ${CMAKE_SOURCE_DIR}/src/examples)

add_library(audio_gui STATIC ${SRC_DIR}/render_portaudio_controls.cpp)
set_target_properties(audio_gui PROPERTIES CXX_STANDARD 17)
target_include_directories(audio_gui PRIVATE ${SRC_DIR} ${ROOT_DIR} ${EXAMPLES_DIR})
target_link_libraries(audio_gui PRIVATE imgui audio_lib)
