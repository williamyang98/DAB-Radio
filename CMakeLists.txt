cmake_minimum_required(VERSION 3.10)
project(ofdm_demod)

set(CMAKE_MODULE_PATH "${CMAKE_MODULE_PATH};${CMAKE_CURRENT_SOURCE_DIR}/cmake")
find_package(imgui REQUIRED)
find_package(implot REQUIRED)
find_package(faad2 REQUIRED)
find_package(kissfft REQUIRED)
find_package(easyloggingpp REQUIRED)
find_package(fmt REQUIRED)

target_compile_definitions(easyloggingpp PRIVATE ELPP_THREAD_SAFE)

if(WIN32)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /fp:fast")
endif(WIN32)

# Our core libraries
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
target_link_libraries(ofdm_core PRIVATE kissfft)

set(DAB_CORE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src/dab)
add_library(dab_core STATIC
    ${DAB_CORE_DIR}/algorithms/viterbi_decoder.cpp
    ${DAB_CORE_DIR}/algorithms/reed_solomon_decoder.cpp
    ${DAB_CORE_DIR}/fic/fic_decoder.cpp
    ${DAB_CORE_DIR}/fic/fig_processor.cpp
    ${DAB_CORE_DIR}/database/dab_database.cpp
    ${DAB_CORE_DIR}/database/dab_database_updater.cpp
    ${DAB_CORE_DIR}/radio_fig_handler.cpp
    ${DAB_CORE_DIR}/msc/msc_decoder.cpp
    ${DAB_CORE_DIR}/msc/cif_deinterleaver.cpp
    ${DAB_CORE_DIR}/audio/aac_frame_processor.cpp
    ${DAB_CORE_DIR}/audio/aac_decoder.cpp)
set_target_properties(dab_core PROPERTIES CXX_STANDARD 17)
include_directories(dab_core PUBLIC ${DAB_CORE_DIR})
target_link_libraries(dab_core PRIVATE faad2 easyloggingpp fmt)
target_compile_definitions(dab_core PRIVATE ELPP_THREAD_SAFE)

add_library(getopt STATIC src/getopt/getopt.c)
include_directories(getopt PUBLIC src/getopt/)

# Our applications
add_executable(view_data 
    src/view_data.cpp
    src/gui/render_ofdm_demod.cpp
    src/gui/imgui_skeleton.cpp)
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

add_executable(process_frames
    src/process_frames.cpp
    src/basic_radio.cpp
    src/audio/win32_pcm_player.cpp
    src/gui/render_basic_radio.cpp
    src/gui/imgui_skeleton.cpp)
include_directories(process_frames src/)
set_target_properties(process_frames PROPERTIES CXX_STANDARD 17)
target_link_libraries(process_frames PRIVATE getopt dab_core imgui easyloggingpp fmt)
target_compile_definitions(process_frames PRIVATE ELPP_THREAD_SAFE)

add_executable(basic_radio_app
    src/basic_radio_app.cpp
    src/basic_radio.cpp
    src/gui/render_basic_radio.cpp
    src/gui/render_ofdm_demod.cpp
    src/gui/imgui_skeleton.cpp
    src/audio/win32_pcm_player.cpp)
include_directories(basic_radio_app src/)
set_target_properties(basic_radio_app PROPERTIES CXX_STANDARD 17)
target_link_libraries(basic_radio_app PRIVATE 
    ofdm_core dab_core 
    getopt imgui implot 
    easyloggingpp fmt)
target_compile_definitions(basic_radio_app PRIVATE ELPP_THREAD_SAFE)

if (WIN32)
target_compile_options(ofdm_core            PRIVATE "/MP")
target_compile_options(dab_core             PRIVATE "/MP")
target_compile_options(getopt               PRIVATE "/MP")

target_compile_options(simulate_transmitter PRIVATE "/MP")
target_compile_options(view_data            PRIVATE "/MP")
target_compile_options(read_data            PRIVATE "/MP")
target_compile_options(process_frames       PRIVATE "/MP")
target_compile_options(basic_radio_app      PRIVATE "/MP")
endif(WIN32)
