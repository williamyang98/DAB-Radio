cmake_minimum_required(VERSION 3.10)
project(examples)

add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/audio)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/device)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/getopt)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/gui)

set(SRC_DIR ${CMAKE_CURRENT_LIST_DIR})
set(ROOT_DIR ${CMAKE_SOURCE_DIR}/src)

function(init_example target)
    target_include_directories(${target} PRIVATE ${SRC_DIR} ${ROOT_DIR})
    set_target_properties(${target} PROPERTIES CXX_STANDARD 17)
    target_compile_definitions(${target} PRIVATE ELPP_THREAD_SAFE)
endfunction()

function(install_dlls target)
    if(DEFINED WIN32)
        add_custom_command(
            TARGET ${target}
            POST_BUILD 
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:${target}> 
            $<TARGET_FILE_DIR:${target}>
            COMMAND_EXPAND_LISTS
        )
    endif()
endfunction()

# Utility applications
if(NOT DEFINED RTLSDR_LIBS)
    message(FATAL_ERROR "RTLSDR_LIBS must be defined")
endif()
add_executable(rtl_sdr ${SRC_DIR}/rtl_sdr.cpp)
init_example(rtl_sdr)
target_link_libraries(rtl_sdr PRIVATE ${RTLSDR_LIBS} getopt)
install_dlls(rtl_sdr)

add_executable(simulate_transmitter ${SRC_DIR}/simulate_transmitter.cpp)
init_example(simulate_transmitter)
target_link_libraries(simulate_transmitter PRIVATE ofdm_core getopt)

add_executable(convert_viterbi ${SRC_DIR}/convert_viterbi.cpp)
init_example(convert_viterbi)
target_link_libraries(convert_viterbi PRIVATE getopt)

add_executable(apply_frequency_shift ${SRC_DIR}/apply_frequency_shift.cpp)
init_example(apply_frequency_shift)
target_link_libraries(apply_frequency_shift PRIVATE getopt ofdm_core)

add_executable(read_wav ${SRC_DIR}/read_wav.cpp)
init_example(read_wav)
target_link_libraries(read_wav PRIVATE getopt)

# Example applications
add_executable(ofdm_demod_gui 
    ${SRC_DIR}/ofdm_demod_gui.cpp
    ${SRC_DIR}/gui/imgui_skeleton.cpp)
init_example(ofdm_demod_gui)
target_link_libraries(ofdm_demod_gui PRIVATE 
    ofdm_core getopt 
    ofdm_gui profiler_gui 
    imgui implot)

add_executable(ofdm_demod_cli ${SRC_DIR}/ofdm_demod_cli.cpp)
init_example(ofdm_demod_cli)
target_link_libraries(ofdm_demod_cli PRIVATE 
    ofdm_core getopt)

add_executable(basic_radio_app_no_demod
    ${SRC_DIR}/basic_radio_app_no_demod.cpp
    ${SRC_DIR}/gui/imgui_skeleton.cpp)
init_example(basic_radio_app_no_demod)
target_link_libraries(basic_radio_app_no_demod PRIVATE 
    dab_core basic_radio audio_lib
    getopt easyloggingpp fmt
    basic_radio_gui audio_gui 
    imgui)

add_executable(basic_radio_app
    ${SRC_DIR}/basic_radio_app.cpp
    ${SRC_DIR}/gui/imgui_skeleton.cpp)
init_example(basic_radio_app)
target_link_libraries(basic_radio_app PRIVATE 
    ofdm_core dab_core basic_radio audio_lib
    getopt easyloggingpp fmt
    ofdm_gui basic_radio_gui audio_gui profiler_gui 
    imgui implot)

add_executable(basic_radio_scraper
    ${SRC_DIR}/basic_radio_scraper.cpp)
init_example(basic_radio_scraper)
target_link_libraries(basic_radio_scraper PRIVATE 
    ofdm_core dab_core basic_radio basic_scraper
    getopt easyloggingpp fmt)

add_executable(basic_radio_scraper_no_demod
    ${SRC_DIR}/basic_radio_scraper_no_demod.cpp)
init_example(basic_radio_scraper_no_demod)
target_link_libraries(basic_radio_scraper_no_demod PRIVATE 
    dab_core basic_radio basic_scraper
    getopt easyloggingpp fmt)

add_executable(basic_radio_benchmark ${SRC_DIR}/basic_radio_benchmark.cpp)
init_example(basic_radio_benchmark)
target_link_libraries(basic_radio_benchmark PRIVATE 
    dab_core basic_radio
    getopt easyloggingpp fmt)

add_executable(radio_app 
    ${SRC_DIR}/radio_app.cpp 
    ${SRC_DIR}/gui/imgui_skeleton.cpp)
init_example(radio_app)
target_link_libraries(radio_app PRIVATE 
    device_lib ofdm_core dab_core basic_radio audio_lib
    getopt fmt easyloggingpp
    device_gui ofdm_gui basic_radio_gui audio_gui profiler_gui 
    imgui implot)
install_dlls(radio_app)