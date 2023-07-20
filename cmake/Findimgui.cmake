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
    # "${SRC_DIR}/backends/imgui_impl_dx9.h"
    # "${SRC_DIR}/backends/imgui_impl_dx9.cpp"
    # "${SRC_DIR}/backends/imgui_impl_dx11.h"
    # "${SRC_DIR}/backends/imgui_impl_dx11.cpp"
    # "${SRC_DIR}/backends/imgui_impl_dx12.h"
    # "${SRC_DIR}/backends/imgui_impl_dx12.cpp"
    # "${SRC_DIR}/backends/imgui_impl_win32.h"
    # "${SRC_DIR}/backends/imgui_impl_win32.cpp"
    "${SRC_DIR}/backends/imgui_impl_opengl3.h"
    "${SRC_DIR}/backends/imgui_impl_opengl3.cpp"
    "${SRC_DIR}/backends/imgui_impl_glfw.h"
    "${SRC_DIR}/backends/imgui_impl_glfw.cpp"
    # "${SRC_DIR}/backends/imgui_impl_vulkan.h"
    # "${SRC_DIR}/backends/imgui_impl_vulkan.cpp"
    # "${SRC_DIR}/backends/imgui_impl_opengl3.h"
    # "${SRC_DIR}/backends/imgui_impl_opengl3.cpp"
)

find_package(glfw3 CONFIG REQUIRED)
# find_package(Vulkan REQUIRED)
find_package(OpenGL REQUIRED)

# set(IMGUI_DXLIBS glfw Vulkan::Vulkan)

if (WIN32)
set(IMGUI_DXLIBS glfw opengl32.lib)
else()
set(IMGUI_DXLIBS glfw GL dl)
endif()

# set(IMGUI_DXLIBS "d3d9.lib" "dxgi.lib" "d3dcompiler.lib")
# set(IMGUI_DXLIBS "d3d11.lib" "dxgi.lib" "d3dcompiler.lib")
# set(IMGUI_DXLIBS "d3d12.lib" "dxgi.lib" "d3dcompiler.lib")

target_include_directories(imgui PUBLIC "${SRC_DIR}")
target_include_directories(imgui PUBLIC "${SRC_DIR}/backends")
target_include_directories(imgui PUBLIC "${SRC_DIR}/misc/cpp")
target_link_libraries(imgui PUBLIC ${IMGUI_DXLIBS})

# Disable multiprocessing since this break debug builds for msbuild
# target_compile_definitions(imgui PUBLIC "/MP")

# add_executable(imgui_example "examples/example_win32_directx9/main.cpp")
# add_executable(imgui_example "examples/example_win32_directx11/main.cpp")
# add_executable(imgui_example "examples/example_win32_directx12/main.cpp")
# add_executable(imgui_example "examples/example_glfw_vulkan/main.cpp")
# add_executable(imgui_example "examples/example_glfw_opengl3/main.cpp")
# target_include_directories(imgui_example PRIVATE "backends/")
# target_link_libraries(imgui_example PRIVATE imgui)
# target_link_libraries(imgui_example PRIVATE ${IMGUI_DXLIBS})
# target_compile_definitions(imgui_example PUBLIC "/MP")
