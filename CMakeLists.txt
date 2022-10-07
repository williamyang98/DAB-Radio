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
    src/ofdm_modulator.cpp
    src/ofdm_symbol_mapper.cpp
    src/dab_prs_ref.cpp
    src/dab_ofdm_params_ref.cpp
    src/dab_mapper_ref.cpp
)

add_library(ofdm_core STATIC ${SRC_FILES})
include_directories(ofdm_core src/)
target_link_libraries(ofdm_core PUBLIC unofficial::kissfft::kissfft)

add_library(getopt STATIC src/getopt/getopt.c)
include_directories(getopt src/getopt/)


add_executable(view_data 
    src/view_data.cpp)
include_directories(view_data src/)
set_target_properties(view_data PROPERTIES CXX_STANDARD 17)
target_link_libraries(view_data PUBLIC ofdm_core getopt imgui implot)


add_executable(read_data 
    src/read_data.cpp)
include_directories(read_data src/)
set_target_properties(read_data PROPERTIES CXX_STANDARD 17)
target_link_libraries(read_data PUBLIC ofdm_core getopt)

add_executable(simulate_transmitter 
    src/simulate_transmitter.cpp)
include_directories(simulate_transmitter src/)
set_target_properties(simulate_transmitter PROPERTIES CXX_STANDARD 17)
target_link_libraries(simulate_transmitter PUBLIC ofdm_core getopt)

add_executable(view_ofdm_sample 
    src/view_ofdm_sample.cpp)
include_directories(view_ofdm_sample src/)
set_target_properties(view_ofdm_sample PROPERTIES CXX_STANDARD 17)
target_link_libraries(view_ofdm_sample PUBLIC getopt imgui implot unofficial::kissfft::kissfft)

add_executable(process_frames
    src/process_frames.cpp
    src/viterbi_decoder.cpp)
include_directories(process_frames src/)
set_target_properties(process_frames PROPERTIES CXX_STANDARD 17)
target_link_libraries(process_frames PUBLIC getopt)

if (WIN32)
target_compile_options(ofdm_core            PRIVATE "/MP")
target_compile_options(getopt               PRIVATE "/MP")

target_compile_options(view_data            PRIVATE "/MP")
target_compile_options(read_data            PRIVATE "/MP")
target_compile_options(simulate_transmitter PRIVATE "/MP")
target_compile_options(view_ofdm_sample     PRIVATE "/MP")
target_compile_options(process_frames       PRIVATE "/MP")
endif(WIN32)