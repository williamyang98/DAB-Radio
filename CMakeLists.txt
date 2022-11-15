cmake_minimum_required(VERSION 3.10)
project(ofdm_demod)

set(CMAKE_MODULE_PATH "${CMAKE_MODULE_PATH};${CMAKE_CURRENT_SOURCE_DIR}/cmake")
find_package(imgui REQUIRED)
find_package(implot REQUIRED)
find_package(faad2 REQUIRED)
find_package(kissfft REQUIRED)
find_package(easyloggingpp REQUIRED)
find_package(fmt REQUIRED)

set(rtlsdr_DIR ${CMAKE_CURRENT_SOURCE_DIR}/vendor/librtlsdr)
find_package(rtlsdr CONFIG REQUIRED)

target_compile_definitions(easyloggingpp PRIVATE ELPP_THREAD_SAFE)

if(MSVC)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /fp:fast")
add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
elseif(UNIX)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -msse2 -msse4.1 -ffast-math")
endif()

set(SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)

# Our core libraries
set(OFDM_CORE_DIR ${SRC_DIR}/modules/ofdm)
add_library(ofdm_core STATIC 
    ${OFDM_CORE_DIR}/ofdm_demodulator.cpp
    ${OFDM_CORE_DIR}/ofdm_demodulator_threads.cpp
    ${OFDM_CORE_DIR}/ofdm_modulator.cpp
    ${OFDM_CORE_DIR}/dab_prs_ref.cpp
    ${OFDM_CORE_DIR}/dab_ofdm_params_ref.cpp
    ${OFDM_CORE_DIR}/dab_mapper_ref.cpp
    ${OFDM_CORE_DIR}/quantized_oscillator.cpp)
target_include_directories(ofdm_core PUBLIC ${OFDM_CORE_DIR} ${SRC_DIR})
set_target_properties(ofdm_core PROPERTIES CXX_STANDARD 17)
target_link_libraries(ofdm_core PRIVATE kissfft fmt)

set(DAB_CORE_DIR ${SRC_DIR}/modules/dab)
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
    ${DAB_CORE_DIR}/radio_fig_handler.cpp)
set_target_properties(dab_core PROPERTIES CXX_STANDARD 17)
target_include_directories(dab_core PUBLIC ${DAB_CORE_DIR} ${SRC_DIR})
target_link_libraries(dab_core PRIVATE faad2 easyloggingpp fmt)
target_compile_definitions(dab_core PRIVATE ELPP_THREAD_SAFE)

set(BASIC_RADIO_DIR ${SRC_DIR}/modules/basic_radio)
add_library(basic_radio STATIC
    ${BASIC_RADIO_DIR}/basic_radio.cpp
    ${BASIC_RADIO_DIR}/basic_threaded_channel.cpp
    ${BASIC_RADIO_DIR}/basic_fic_runner.cpp
    ${BASIC_RADIO_DIR}/basic_dab_plus_channel.cpp
    ${BASIC_RADIO_DIR}/basic_slideshow.cpp
    ${BASIC_RADIO_DIR}/basic_database_manager.cpp)
set_target_properties(basic_radio PROPERTIES CXX_STANDARD 17)
target_include_directories(basic_radio PUBLIC ${BASIC_RADIO_DIR} ${SRC_DIR})
target_link_libraries(basic_radio PRIVATE dab_core easyloggingpp fmt)
target_compile_definitions(basic_radio PRIVATE ELPP_THREAD_SAFE)

set(BASIC_RADIO_GUI_DIR ${SRC_DIR}/gui/basic_radio)
add_library(basic_radio_gui STATIC
    ${BASIC_RADIO_GUI_DIR}/render_simple_view.cpp
    ${BASIC_RADIO_GUI_DIR}/render_common.cpp
    ${BASIC_RADIO_GUI_DIR}/simple_view_controller.cpp
    ${BASIC_RADIO_GUI_DIR}/texture.cpp
    ${BASIC_RADIO_GUI_DIR}/formatters.cpp)
set_target_properties(basic_radio_gui PROPERTIES CXX_STANDARD 17)
target_include_directories(basic_radio_gui PUBLIC 
    ${BASIC_RADIO_DIR} ${SRC_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor/stb)
target_link_libraries(basic_radio_gui PRIVATE 
    dab_core imgui 
    easyloggingpp fmt)
target_compile_definitions(basic_radio_gui PRIVATE ELPP_THREAD_SAFE)

set(GETOPT_DIR ${SRC_DIR}/utility/getopt)
add_library(getopt STATIC ${GETOPT_DIR}/getopt.c)
target_include_directories(getopt PUBLIC ${GETOPT_DIR})

# Our applications
add_executable(ofdm_demod_gui 
    ${SRC_DIR}/ofdm_demod_gui.cpp
    ${SRC_DIR}/gui/render_ofdm_demod.cpp
    ${SRC_DIR}/gui/imgui_skeleton.cpp)
target_include_directories(ofdm_demod_gui PUBLIC ${SRC_DIR})
set_target_properties(ofdm_demod_gui PROPERTIES CXX_STANDARD 17)
target_link_libraries(ofdm_demod_gui PRIVATE ofdm_core getopt imgui implot)

add_executable(ofdm_demod_cli 
    ${SRC_DIR}/ofdm_demod_cli.cpp)
target_include_directories(ofdm_demod_cli PUBLIC ${SRC_DIR})
set_target_properties(ofdm_demod_cli PROPERTIES CXX_STANDARD 17)
target_link_libraries(ofdm_demod_cli PRIVATE ofdm_core getopt)

add_executable(basic_radio_app_no_demod
    ${SRC_DIR}/basic_radio_app_no_demod.cpp
    ${SRC_DIR}/gui/imgui_skeleton.cpp
    ${SRC_DIR}/audio/win32_pcm_player.cpp)
target_include_directories(basic_radio_app_no_demod PUBLIC ${SRC_DIR})
set_target_properties(basic_radio_app_no_demod PROPERTIES CXX_STANDARD 17)
target_link_libraries(basic_radio_app_no_demod PRIVATE 
    dab_core basic_radio
    getopt easyloggingpp fmt
    basic_radio_gui imgui)
target_compile_definitions(basic_radio_app_no_demod PRIVATE ELPP_THREAD_SAFE)

add_executable(basic_radio_app
    ${SRC_DIR}/basic_radio_app.cpp
    ${SRC_DIR}/gui/render_ofdm_demod.cpp
    ${SRC_DIR}/gui/imgui_skeleton.cpp
    ${SRC_DIR}/audio/win32_pcm_player.cpp)
target_include_directories(basic_radio_app PUBLIC ${SRC_DIR})
set_target_properties(basic_radio_app PROPERTIES CXX_STANDARD 17)
target_link_libraries(basic_radio_app PRIVATE 
    ofdm_core dab_core basic_radio
    getopt easyloggingpp fmt
    basic_radio_gui imgui implot)
target_compile_definitions(basic_radio_app PRIVATE ELPP_THREAD_SAFE)

add_executable(basic_radio_scraper
    ${SRC_DIR}/basic_radio_scraper.cpp
    ${SRC_DIR}/modules/basic_scraper/basic_scraper.cpp)
target_include_directories(basic_radio_scraper PUBLIC ${SRC_DIR})
set_target_properties(basic_radio_scraper PROPERTIES CXX_STANDARD 17)
target_link_libraries(basic_radio_scraper PRIVATE 
    ofdm_core dab_core basic_radio
    getopt easyloggingpp fmt)
target_compile_definitions(basic_radio_scraper PRIVATE ELPP_THREAD_SAFE)

add_executable(basic_radio_scraper_no_demod
    ${SRC_DIR}/basic_radio_scraper_no_demod.cpp
    ${SRC_DIR}/modules/basic_scraper/basic_scraper.cpp)
target_include_directories(basic_radio_scraper_no_demod PUBLIC ${SRC_DIR})
set_target_properties(basic_radio_scraper_no_demod PROPERTIES CXX_STANDARD 17)
target_link_libraries(basic_radio_scraper_no_demod PRIVATE 
    dab_core basic_radio
    getopt easyloggingpp fmt)
target_compile_definitions(basic_radio_scraper_no_demod PRIVATE ELPP_THREAD_SAFE)

add_executable(simulate_transmitter 
    ${SRC_DIR}/simulate_transmitter.cpp)
target_include_directories(simulate_transmitter PUBLIC ${SRC_DIR})
set_target_properties(simulate_transmitter PROPERTIES CXX_STANDARD 17)
target_link_libraries(simulate_transmitter PRIVATE ofdm_core getopt)

add_executable(convert_viterbi
    ${SRC_DIR}/convert_viterbi.cpp)
target_include_directories(convert_viterbi PUBLIC ${SRC_DIR})
set_target_properties(convert_viterbi PROPERTIES CXX_STANDARD 17)
target_link_libraries(convert_viterbi PRIVATE getopt)

add_executable(apply_frequency_shift 
    ${SRC_DIR}/apply_frequency_shift.cpp
    ${OFDM_CORE_DIR}/quantized_oscillator.cpp)
target_include_directories(apply_frequency_shift PUBLIC ${SRC_DIR} ${OFDM_CORE_DIR})
set_target_properties(apply_frequency_shift PROPERTIES CXX_STANDARD 17)
target_link_libraries(apply_frequency_shift PRIVATE getopt)

add_executable(radio_app 
    ${SRC_DIR}/radio_app.cpp 
    ${SRC_DIR}/modules/device/device_selector.cpp
    ${SRC_DIR}/modules/device/device.cpp
    ${SRC_DIR}/gui/imgui_skeleton.cpp
    ${SRC_DIR}/gui/render_device_selector.cpp
    ${SRC_DIR}/gui/render_ofdm_demod.cpp
    ${SRC_DIR}/audio/win32_pcm_player.cpp)
target_include_directories(radio_app PUBLIC ${SRC_DIR})
set_target_properties(radio_app PROPERTIES CXX_STANDARD 17)
target_link_libraries(radio_app PRIVATE 
    rtlsdr ofdm_core dab_core basic_radio
    getopt fmt easyloggingpp
    basic_radio_gui imgui implot)
target_compile_definitions(radio_app PRIVATE ELPP_THREAD_SAFE)

# vcpkg.cmake has internal stuff that autogenerates this
# we have to do this manually
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    add_custom_command(TARGET radio_app 
        POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${rtlsdr_DIR}/bin/x64/
        $<TARGET_FILE_DIR:radio_app>)
elseif(CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "x86 rtlsdr is unsupported")
endif()

if(MSVC)
target_compile_options(ofdm_core            PRIVATE "/MP")
target_compile_options(dab_core             PRIVATE "/MP")
target_compile_options(basic_radio          PRIVATE "/MP")
target_compile_options(basic_radio_gui      PRIVATE "/MP")
target_compile_options(getopt               PRIVATE "/MP")

target_compile_options(ofdm_demod_gui       PRIVATE "/MP")
target_compile_options(ofdm_demod_cli       PRIVATE "/MP")
target_compile_options(basic_radio_app_no_demod PRIVATE "/MP")
target_compile_options(basic_radio_app      PRIVATE "/MP")
target_compile_options(basic_radio_scraper  PRIVATE "/MP")
target_compile_options(basic_radio_scraper_no_demod PRIVATE "/MP")

target_compile_options(simulate_transmitter PRIVATE "/MP")
target_compile_options(convert_viterbi      PRIVATE "/MP")
target_compile_options(apply_frequency_shift PRIVATE "/MP")
target_compile_options(radio_app            PRIVATE "/MP")

endif()
