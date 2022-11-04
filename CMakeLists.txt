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
    ${OFDM_CORE_DIR}/ofdm_demodulator_threads.cpp
    ${OFDM_CORE_DIR}/ofdm_modulator.cpp
    ${OFDM_CORE_DIR}/dab_prs_ref.cpp
    ${OFDM_CORE_DIR}/dab_ofdm_params_ref.cpp
    ${OFDM_CORE_DIR}/dab_mapper_ref.cpp
    ${OFDM_CORE_DIR}/quantized_oscillator.cpp)
include_directories(ofdm_core PUBLIC ${OFDM_CORE_DIR})
set_target_properties(ofdm_core PROPERTIES CXX_STANDARD 17)
target_link_libraries(ofdm_core PRIVATE kissfft)

set(DAB_CORE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src/dab)
add_library(dab_core STATIC
    ${DAB_CORE_DIR}/algorithms/viterbi_decoder.cpp
    ${DAB_CORE_DIR}/algorithms/phil_karn_viterbi_decoder.cpp
    ${DAB_CORE_DIR}/algorithms/reed_solomon_decoder.cpp
    ${DAB_CORE_DIR}/fic/fic_decoder.cpp
    ${DAB_CORE_DIR}/fic/fig_processor.cpp
    ${DAB_CORE_DIR}/database/dab_database.cpp
    ${DAB_CORE_DIR}/database/dab_database_updater.cpp
    ${DAB_CORE_DIR}/msc/msc_decoder.cpp
    ${DAB_CORE_DIR}/msc/cif_deinterleaver.cpp
    ${DAB_CORE_DIR}/msc/msc_xpad_processor.cpp
    ${DAB_CORE_DIR}/audio/aac_frame_processor.cpp
    ${DAB_CORE_DIR}/audio/aac_audio_decoder.cpp
    ${DAB_CORE_DIR}/audio/aac_data_decoder.cpp
    ${DAB_CORE_DIR}/mot/MOT_assembler.cpp
    ${DAB_CORE_DIR}/mot/MOT_processor.cpp
    ${DAB_CORE_DIR}/mot/MOT_slideshow_processor.cpp
    ${DAB_CORE_DIR}/pad/pad_data_group.cpp
    ${DAB_CORE_DIR}/pad/pad_data_length_indicator.cpp
    ${DAB_CORE_DIR}/pad/pad_dynamic_label_assembler.cpp
    ${DAB_CORE_DIR}/pad/pad_dynamic_label.cpp
    ${DAB_CORE_DIR}/pad/pad_MOT_processor.cpp
    ${DAB_CORE_DIR}/pad/pad_processor.cpp
    ${DAB_CORE_DIR}/radio_fig_handler.cpp
)
set_target_properties(dab_core PROPERTIES CXX_STANDARD 17)
include_directories(dab_core PUBLIC ${DAB_CORE_DIR})
target_link_libraries(dab_core PRIVATE faad2 easyloggingpp fmt)
target_compile_definitions(dab_core PRIVATE ELPP_THREAD_SAFE)

set(BASIC_RADIO_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src/basic_radio)
add_library(basic_radio STATIC
    ${BASIC_RADIO_DIR}/basic_radio.cpp
    ${BASIC_RADIO_DIR}/basic_threaded_channel.cpp
    ${BASIC_RADIO_DIR}/basic_fic_runner.cpp
    ${BASIC_RADIO_DIR}/basic_audio_channel.cpp
    ${BASIC_RADIO_DIR}/basic_slideshow.cpp
    ${BASIC_RADIO_DIR}/basic_database_manager.cpp
)
set_target_properties(basic_radio PROPERTIES CXX_STANDARD 17)
include_directories(basic_radio PUBLIC ${BASIC_RADIO_DIR})
target_link_libraries(basic_radio PRIVATE dab_core easyloggingpp fmt)
target_compile_definitions(basic_radio PRIVATE ELPP_THREAD_SAFE)

add_library(getopt STATIC src/getopt/getopt.c)
include_directories(getopt PUBLIC src/getopt/)

# Our applications
add_executable(ofdm_demod_gui 
    src/ofdm_demod_gui.cpp
    src/gui/render_ofdm_demod.cpp
    src/gui/imgui_skeleton.cpp)
include_directories(ofdm_demod_gui src/)
set_target_properties(ofdm_demod_gui PROPERTIES CXX_STANDARD 17)
target_link_libraries(ofdm_demod_gui PRIVATE ofdm_core getopt imgui implot)

add_executable(ofdm_demod_cli 
    src/ofdm_demod_cli.cpp)
include_directories(ofdm_demod_cli src/)
set_target_properties(ofdm_demod_cli PROPERTIES CXX_STANDARD 17)
target_link_libraries(ofdm_demod_cli PRIVATE ofdm_core getopt)

add_executable(simulate_transmitter 
    src/simulate_transmitter.cpp)
include_directories(simulate_transmitter src/)
set_target_properties(simulate_transmitter PROPERTIES CXX_STANDARD 17)
target_link_libraries(simulate_transmitter PRIVATE ofdm_core getopt)

set(BASIC_RADIO_GUI_SRC 
    src/gui/basic_radio/render_basic_radio.cpp
    src/gui/basic_radio/render_simple_view.cpp
    src/gui/basic_radio/render_common.cpp
    src/gui/basic_radio/formatters.cpp)

add_executable(basic_radio_app_no_demod
    src/basic_radio_app_no_demod.cpp
    src/gui/imgui_skeleton.cpp
    src/audio/win32_pcm_player.cpp
    ${BASIC_RADIO_GUI_SRC}
)
include_directories(basic_radio_app_no_demod src/)
set_target_properties(basic_radio_app_no_demod PROPERTIES CXX_STANDARD 17)
target_link_libraries(basic_radio_app_no_demod PRIVATE 
    dab_core basic_radio
    getopt imgui easyloggingpp fmt)
target_compile_definitions(basic_radio_app_no_demod PRIVATE ELPP_THREAD_SAFE)

add_executable(basic_radio_app
    src/basic_radio_app.cpp
    src/gui/render_ofdm_demod.cpp
    src/gui/imgui_skeleton.cpp
    src/audio/win32_pcm_player.cpp
    ${BASIC_RADIO_GUI_SRC}
)
include_directories(basic_radio_app src/)
set_target_properties(basic_radio_app PROPERTIES CXX_STANDARD 17)
target_link_libraries(basic_radio_app PRIVATE 
    ofdm_core dab_core basic_radio
    getopt imgui implot 
    easyloggingpp fmt)
target_compile_definitions(basic_radio_app PRIVATE ELPP_THREAD_SAFE)

if (WIN32)
target_compile_options(ofdm_core            PRIVATE "/MP")
target_compile_options(dab_core             PRIVATE "/MP")
target_compile_options(basic_radio          PRIVATE "/MP")
target_compile_options(getopt               PRIVATE "/MP")

target_compile_options(simulate_transmitter PRIVATE "/MP")
target_compile_options(ofdm_demod_gui       PRIVATE "/MP")
target_compile_options(ofdm_demod_cli       PRIVATE "/MP")
target_compile_options(basic_radio_app_no_demod PRIVATE "/MP")
target_compile_options(basic_radio_app      PRIVATE "/MP")
endif(WIN32)
