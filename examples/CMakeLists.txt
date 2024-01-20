cmake_minimum_required(VERSION 3.10)
project(examples)

add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/audio)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/device)
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
target_link_libraries(rtl_sdr PRIVATE ${RTLSDR_LIBS} argparse::argparse)
install_dlls(rtl_sdr)

add_executable(simulate_transmitter ${SRC_DIR}/simulate_transmitter.cpp)
init_example(simulate_transmitter)
target_link_libraries(simulate_transmitter PRIVATE ofdm_core argparse::argparse)

add_executable(convert_viterbi ${SRC_DIR}/convert_viterbi.cpp)
init_example(convert_viterbi)
target_link_libraries(convert_viterbi PRIVATE argparse::argparse)

add_executable(apply_frequency_shift ${SRC_DIR}/apply_frequency_shift.cpp)
init_example(apply_frequency_shift)
target_link_libraries(apply_frequency_shift PRIVATE argparse::argparse ofdm_core)

add_executable(read_wav ${SRC_DIR}/read_wav.cpp)
init_example(read_wav)
target_link_libraries(read_wav PRIVATE argparse::argparse)

# Example applications
add_executable(basic_radio_app_cli ${SRC_DIR}/basic_radio_app.cpp)
init_example(basic_radio_app_cli)
target_link_libraries(basic_radio_app_cli PRIVATE 
    argparse::argparse easyloggingpp fmt
    ofdm_core dab_core basic_radio basic_scraper)
target_compile_definitions(basic_radio_app_cli PRIVATE BUILD_COMMAND_LINE)

set(COMMON_GUI_SRC ${SRC_DIR}/app_helpers/app_common_gui.cpp)
add_executable(basic_radio_app ${SRC_DIR}/basic_radio_app.cpp ${COMMON_GUI_SRC})
init_example(basic_radio_app)
target_link_libraries(basic_radio_app PRIVATE 
    argparse::argparse easyloggingpp fmt
    ofdm_core dab_core basic_radio basic_scraper audio_lib
    ofdm_gui basic_radio_gui audio_gui imgui implot)

add_executable(radio_app ${SRC_DIR}/radio_app.cpp ${COMMON_GUI_SRC})
init_example(radio_app)
target_link_libraries(radio_app PRIVATE 
    argparse::argparse easyloggingpp fmt
    device_lib ofdm_core dab_core basic_radio audio_lib basic_scraper
    device_gui ofdm_gui basic_radio_gui audio_gui imgui implot)
install_dlls(radio_app)