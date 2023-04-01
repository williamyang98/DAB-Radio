cmake_minimum_required(VERSION 3.10)

set(SRC_DIR ${CMAKE_CURRENT_LIST_DIR})
set(ROOT_DIR ${CMAKE_SOURCE_DIR}/src)

if(NOT DEFINED FFTW3_LIBS)
    message(FATAL_ERROR "FFTW3_LIBS must be defined")
endif()

if (NOT ${CMAKE_SYSTEM_PROCESSOR} STREQUAL "aarch64")
    # x86 processors 
    set(SRC_SIMD_FILES ${SRC_DIR}/dsp/x86/apply_pll.cpp ${SRC_DIR}/dsp/x86/complex_conj_mul_sum.cpp)
else()
    # arm processors
    set(SRC_SIMD_FILES "")
endif()

add_library(ofdm_core STATIC 
    ${SRC_DIR}/ofdm_demodulator.cpp
    ${SRC_DIR}/ofdm_demodulator_threads.cpp
    ${SRC_DIR}/ofdm_modulator.cpp
    ${SRC_DIR}/dab_prs_ref.cpp
    ${SRC_DIR}/dab_ofdm_params_ref.cpp
    ${SRC_DIR}/dab_mapper_ref.cpp
    ${SRC_DIR}/dsp/apply_pll.cpp
    ${SRC_DIR}/dsp/complex_conj_mul_sum.cpp
    ${SRC_SIMD_FILES}
)
target_include_directories(ofdm_core PRIVATE ${SRC_DIR} ${ROOT_DIR})
set_target_properties(ofdm_core PROPERTIES CXX_STANDARD 17)
target_link_libraries(ofdm_core PRIVATE ${FFTW3_LIBS} fmt)

if (MSVC)
target_compile_options(ofdm_core PRIVATE "/MP")
endif()