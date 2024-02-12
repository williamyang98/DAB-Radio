cmake_minimum_required(VERSION 3.16)
project(imgui)

set(SRC_DIR ${CMAKE_CURRENT_LIST_DIR}/../vendor/imgui)

# imconfig.h is required to use 32bit vertex indices
# this is required since implot uses alot of vertices
configure_file(cmake/imgui_imconfig.h.in ${SRC_DIR}/imconfig.h)

add_library(imgui STATIC
    "${SRC_DIR}/imgui.h"
    "${SRC_DIR}/imgui_internal.h"
    "${SRC_DIR}/imgui.cpp"
    "${SRC_DIR}/imgui_demo.cpp"
    "${SRC_DIR}/imgui_draw.cpp"
    "${SRC_DIR}/imgui_widgets.cpp"
    "${SRC_DIR}/imgui_tables.cpp"
    "${SRC_DIR}/misc/cpp/imgui_stdlib.h"
    "${SRC_DIR}/misc/cpp/imgui_stdlib.cpp"
    "${SRC_DIR}/backends/imgui_impl_opengl3.h"
    "${SRC_DIR}/backends/imgui_impl_opengl3.cpp"
    "${SRC_DIR}/backends/imgui_impl_glfw.h"
    "${SRC_DIR}/backends/imgui_impl_glfw.cpp"
)

find_package(glfw3 CONFIG REQUIRED)
find_package(OpenGL REQUIRED)

target_include_directories(imgui PUBLIC "${SRC_DIR}")
target_include_directories(imgui PUBLIC "${SRC_DIR}/backends")
target_include_directories(imgui PUBLIC "${SRC_DIR}/misc/cpp")
target_link_libraries(imgui PUBLIC ${IMGUI_DXLIBS})
target_compile_features(imgui PUBLIC cxx_std_17)
if(WIN32)
    target_link_libraries(imgui PUBLIC glfw OpenGL::GL)
else()
    target_link_libraries(imgui PUBLIC glfw ${OPENGL_LIBRARIES} dl)
endif()
