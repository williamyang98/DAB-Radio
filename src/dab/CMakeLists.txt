cmake_minimum_required(VERSION 3.10)
project(dab_core)

option(DAB_CORE_USE_EASYLOGGING "Use easylogging for dab_core" OFF)

set(SRC_DIR ${CMAKE_CURRENT_LIST_DIR})
set(ROOT_DIR ${SRC_DIR}/..)

add_library(dab_core STATIC
    ${SRC_DIR}/algorithms/dab_viterbi_decoder.cpp
    ${SRC_DIR}/algorithms/reed_solomon_decoder.cpp
    ${SRC_DIR}/fic/fic_decoder.cpp
    ${SRC_DIR}/fic/fig_processor.cpp
    ${SRC_DIR}/database/dab_database_updater.cpp
    ${SRC_DIR}/msc/msc_decoder.cpp
    ${SRC_DIR}/msc/cif_deinterleaver.cpp
    ${SRC_DIR}/msc/msc_data_group_processor.cpp
    ${SRC_DIR}/msc/msc_data_packet_processor.cpp
    ${SRC_DIR}/msc/msc_reed_solomon_data_packet_processor.cpp
    ${SRC_DIR}/audio/aac_frame_processor.cpp
    ${SRC_DIR}/audio/aac_audio_decoder.cpp
    ${SRC_DIR}/audio/aac_data_decoder.cpp
    ${SRC_DIR}/audio/mp2_audio_decoder.cpp
    ${SRC_DIR}/mot/MOT_assembler.cpp
    ${SRC_DIR}/mot/MOT_processor.cpp
    ${SRC_DIR}/mot/MOT_slideshow_processor.cpp
    ${SRC_DIR}/pad/pad_data_group.cpp
    ${SRC_DIR}/pad/pad_data_length_indicator.cpp
    ${SRC_DIR}/pad/pad_dynamic_label_assembler.cpp
    ${SRC_DIR}/pad/pad_dynamic_label.cpp
    ${SRC_DIR}/pad/pad_MOT_processor.cpp
    ${SRC_DIR}/pad/pad_processor.cpp
    ${SRC_DIR}/radio_fig_handler.cpp)
set_target_properties(dab_core PROPERTIES CXX_STANDARD 17)
target_include_directories(dab_core PRIVATE ${SRC_DIR} ${ROOT_DIR})
target_link_libraries(dab_core PRIVATE faad2 viterbi fmt)

if(DAB_CORE_USE_EASYLOGGING)
    target_sources(dab_core PRIVATE ${SRC_DIR}/dab_logging.cpp)
    target_link_libraries(dab_core PRIVATE easyloggingpp)
    target_compile_definitions(dab_core PRIVATE ELPP_THREAD_SAFE)
    target_compile_definitions(dab_core PUBLIC DAB_LOGGING_USE_EASYLOGGING)
endif()