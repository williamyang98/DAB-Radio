cmake_minimum_required(VERSION 3.10)
project(ofdm_demod)

set(CMAKE_MODULE_PATH "${CMAKE_MODULE_PATH};${CMAKE_CURRENT_SOURCE_DIR}/cmake")
find_package(imgui REQUIRED)
find_package(implot REQUIRED)

find_package(unofficial-kissfft CONFIG REQUIRED)

if(WIN32)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /fp:fast")
endif(WIN32)

set(SRC_FILES
    src/ofdm_demodulator.cpp
    src/dab_prs_ref.cpp
    src/dab_ofdm_params_ref.cpp
)

add_executable(view_data 
    ${SRC_FILES}
    src/view_data.cpp
    src/getopt/getopt.c)
include_directories(view_data src/ src/getopt/)
set_target_properties(view_data PROPERTIES CXX_STANDARD 17)
target_link_libraries(view_data PUBLIC imgui implot unofficial::kissfft::kissfft)

add_executable(read_data 
    ${SRC_FILES}
    src/read_data.cpp
    src/getopt/getopt.c)
include_directories(read_data src/ src/getopt/)
set_target_properties(read_data PROPERTIES CXX_STANDARD 17)
target_link_libraries(read_data PUBLIC unofficial::kissfft::kissfft)

add_executable(view_ofdm_sample 
    src/view_ofdm_sample.cpp
    src/getopt/getopt.c)
include_directories(view_ofdm_sample src/ src/getopt/)
set_target_properties(view_ofdm_sample PROPERTIES CXX_STANDARD 17)
target_link_libraries(view_ofdm_sample PUBLIC imgui implot unofficial::kissfft::kissfft)

if (WIN32)
target_compile_options(view_data        PRIVATE "/MP")
target_compile_options(read_data        PRIVATE "/MP")
target_compile_options(view_ofdm_sample PRIVATE "/MP")
endif(WIN32)