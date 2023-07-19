cmake_minimum_required(VERSION 3.10)
project(ofdm_demod)

# NOTE: Root cmake file is used to setup all project dependencies

set(CMAKE_MODULE_PATH "${CMAKE_MODULE_PATH};${CMAKE_CURRENT_SOURCE_DIR}/cmake")
find_package(faad2 REQUIRED)
find_package(easyloggingpp REQUIRED)
find_package(fmt REQUIRED)
find_package(imgui REQUIRED)
find_package(implot REQUIRED)

set(viterbi_DIR ${CMAKE_CURRENT_SOURCE_DIR}/vendor/viterbi_decoder)
find_package(viterbi CONFIG REQUIRED)

if(DEFINED WIN32) 
    set(rtlsdr_DIR ${CMAKE_CURRENT_SOURCE_DIR}/vendor/librtlsdr)
    find_package(rtlsdr CONFIG REQUIRED)
    set(RTLSDR_LIBS rtlsdr::rtlsdr)
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(rtlsdr REQUIRED IMPORTED_TARGET librtlsdr)
    set(RTLSDR_LIBS PkgConfig::rtlsdr)
endif()

if(MSVC)
    find_package(portaudio CONFIG REQUIRED)
    find_package(FFTW3f CONFIG REQUIRED)
    set(PORTAUDIO_LIBS portaudio)
    set(FFTW3_LIBS FFTW3::fftw3f)
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(portaudio REQUIRED IMPORTED_TARGET portaudio-2.0)
    pkg_check_modules(fftw3f REQUIRED IMPORTED_TARGET fftw3f)
    set(PORTAUDIO_LIBS PkgConfig::portaudio)
    set(FFTW3_LIBS PkgConfig::fftw3f)
endif()

target_compile_definitions(easyloggingpp PRIVATE ELPP_THREAD_SAFE)

# for posix threads
if(NOT DEFINED WIN32) 
    find_package(Threads REQUIRED)
    add_compile_options(-pthread) 
    link_libraries(Threads::Threads)
endif()

# simd and math compile options
if(NOT ${CMAKE_SYSTEM_PROCESSOR} STREQUAL "aarch64")
    if(DEFINED MSVC)
        add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
        add_compile_definitions(_SILENCE_NONFLOATING_COMPLEX_DEPRECATION_WARNING)
        # for msvc multi processor compilation
        add_compile_options(/MP)
        # NOTE: The MSVC compiler assumes a conservative microarchitecture and optimises using SSE2 by default
        #       Since this is a performance sensitive application, we want to be aggressive with SIMD optimisations
        add_compile_options(/fp:fast /arch:AVX2)
        # add_compile_options(/fp:fast /arch:AVX)
        # add_compile_options(/fp:fast)
    else()
        # NOTE: We are optimising specifically for your CPU's microarchitecture
        #       This is okay since we don't ship prebuilt binaries for linux distributions
        add_compile_options(-march=native -ffast-math)
    endif()
else()
    add_compile_options(-march=native -ffast-math)
endif()

set(SRC_DIR ${CMAKE_SOURCE_DIR}/src)
add_subdirectory(${SRC_DIR}/dab)
add_subdirectory(${SRC_DIR}/ofdm)
add_subdirectory(${SRC_DIR}/basic_radio)
add_subdirectory(${SRC_DIR}/basic_scraper)
add_subdirectory(${SRC_DIR}/examples)