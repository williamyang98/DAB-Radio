cmake_minimum_required(VERSION 3.10)

set(SRC_DIR ${CMAKE_CURRENT_LIST_DIR})
set(ROOT_DIR ${CMAKE_SOURCE_DIR}/src)

add_library(dab_core STATIC
    ${SRC_DIR}/algorithms/dab_viterbi_decoder.cpp
    ${SRC_DIR}/algorithms/reed_solomon_decoder.cpp
    ${SRC_DIR}/fic/fic_decoder.cpp
    ${SRC_DIR}/fic/fig_processor.cpp
    ${SRC_DIR}/database/dab_database.cpp
    ${SRC_DIR}/database/dab_database_updater.cpp
    ${SRC_DIR}/msc/msc_decoder.cpp
    ${SRC_DIR}/msc/cif_deinterleaver.cpp
    ${SRC_DIR}/msc/msc_xpad_processor.cpp
    ${SRC_DIR}/audio/aac_frame_processor.cpp
    ${SRC_DIR}/audio/aac_audio_decoder.cpp
    ${SRC_DIR}/audio/aac_data_decoder.cpp
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
target_link_libraries(dab_core PRIVATE faad2 viterbi easyloggingpp fmt)
target_compile_definitions(dab_core PRIVATE ELPP_THREAD_SAFE)

if (MSVC)
target_compile_options(dab_core PRIVATE "/MP")
endif()