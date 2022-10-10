cmake_minimum_required(VERSION 3.10)
project(ofdm_demod)

set(CMAKE_MODULE_PATH "${CMAKE_MODULE_PATH};${CMAKE_CURRENT_SOURCE_DIR}/cmake")
find_package(imgui REQUIRED)
find_package(implot REQUIRED)

find_package(unofficial-kissfft CONFIG REQUIRED)

if(WIN32)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /fp:fast")
endif(WIN32)

set(OFDM_CORE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src/ofdm)
add_library(ofdm_core STATIC 
    ${OFDM_CORE_DIR}/ofdm_demodulator.cpp
    ${OFDM_CORE_DIR}/ofdm_modulator.cpp
    ${OFDM_CORE_DIR}/ofdm_symbol_mapper.cpp
    ${OFDM_CORE_DIR}/dab_prs_ref.cpp
    ${OFDM_CORE_DIR}/dab_ofdm_params_ref.cpp
    ${OFDM_CORE_DIR}/dab_mapper_ref.cpp)
include_directories(ofdm_core PUBLIC ${OFDM_CORE_DIR})
set_target_properties(ofdm_core PROPERTIES CXX_STANDARD 17)
target_link_libraries(ofdm_core PRIVATE unofficial::kissfft::kissfft)

set(DAB_CORE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src/dab)
add_library(dab_core STATIC
    ${DAB_CORE_DIR}/viterbi_decoder.cpp
    ${DAB_CORE_DIR}/fic_processor.cpp)
set_target_properties(dab_core PROPERTIES CXX_STANDARD 17)
include_directories(dab_core PUBLIC ${DAB_CORE_DIR})

add_library(getopt STATIC src/getopt/getopt.c)
include_directories(getopt PUBLIC src/getopt/)

add_executable(view_data 
    src/view_data.cpp)
include_directories(view_data src/)
set_target_properties(view_data PROPERTIES CXX_STANDARD 17)
target_link_libraries(view_data PRIVATE ofdm_core getopt imgui implot)


add_executable(read_data 
    src/read_data.cpp)
include_directories(read_data src/)
set_target_properties(read_data PROPERTIES CXX_STANDARD 17)
target_link_libraries(read_data PRIVATE ofdm_core getopt)

add_executable(simulate_transmitter 
    src/simulate_transmitter.cpp)
include_directories(simulate_transmitter src/)
set_target_properties(simulate_transmitter PROPERTIES CXX_STANDARD 17)
target_link_libraries(simulate_transmitter PRIVATE ofdm_core getopt)

add_executable(view_ofdm_sample 
    src/view_ofdm_sample.cpp)
include_directories(view_ofdm_sample src/)
set_target_properties(view_ofdm_sample PROPERTIES CXX_STANDARD 17)
target_link_libraries(view_ofdm_sample PRIVATE getopt imgui implot unofficial::kissfft::kissfft)

add_executable(process_frames
    src/process_frames.cpp)
include_directories(process_frames src/)
set_target_properties(process_frames PROPERTIES CXX_STANDARD 17)
target_link_libraries(process_frames PRIVATE getopt dab_core)

if (WIN32)
target_compile_options(ofdm_core            PRIVATE "/MP")
target_compile_options(dab_core             PRIVATE "/MP")
target_compile_options(getopt               PRIVATE "/MP")

target_compile_options(simulate_transmitter PRIVATE "/MP")
target_compile_options(view_ofdm_sample     PRIVATE "/MP")

target_compile_options(view_data            PRIVATE "/MP")
target_compile_options(read_data            PRIVATE "/MP")
target_compile_options(process_frames       PRIVATE "/MP")
endif(WIN32)