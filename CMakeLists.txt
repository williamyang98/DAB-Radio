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

# Windows + MSYS = Install rtlsdr from submodule
# Ubuntu = Install rtlsdr from package manager
if (WIN32) 
    set(rtlsdr_DIR ${CMAKE_CURRENT_SOURCE_DIR}/vendor/librtlsdr)
    find_package(rtlsdr CONFIG REQUIRED)
    set(RTLSDR_LIBS rtlsdr::rtlsdr)
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(rtlsdr REQUIRED IMPORTED_TARGET librtlsdr)
    set(RTLSDR_LIBS PkgConfig::rtlsdr)
endif()

# MSVC = vcpkg package manager
# MSYS2 + Ubuntu = package manager
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

# TODO: Determine if there is a working CMake third party script that lets us find the most aggressive SIMD options possible
if (NOT ${CMAKE_SYSTEM_PROCESSOR} STREQUAL "aarch64")
    # x86 processors 
    message(STATUS "Setting x86 SIMD CXX_FLAGS and defines")
    if(MSVC)
        add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
        add_compile_definitions(_SILENCE_NONFLOATING_COMPLEX_DEPRECATION_WARNING)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /fp:fast /arch:AVX2")
        # NOTE: If we are on a platform without AVX2
        #set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /fp:fast /arch:AVX")
        #set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /fp:fast")
    else()
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mavx2 -mfma -ffast-math")
        # NOTE: If we are compiling with sse4.2 not avx2+fma (affects pll and viterbi decoder)
        #set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -msse4.2 -ffast-math")
    endif()
else()
    # arm processors
    message(STATUS "Setting aarch64 SIMD CXX_FLAGS and defines")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -ffast-math")
endif()

set(SRC_DIR ${CMAKE_SOURCE_DIR}/src)
add_subdirectory(${SRC_DIR}/dab)
add_subdirectory(${SRC_DIR}/ofdm)
add_subdirectory(${SRC_DIR}/basic_radio)
add_subdirectory(${SRC_DIR}/basic_scraper)
add_subdirectory(${SRC_DIR}/examples)