cmake_minimum_required(VERSION 3.10)
project(dab_radio)

# Setup all subprojects' dependencies
set(CMAKE_MODULE_PATH "${CMAKE_MODULE_PATH};${CMAKE_CURRENT_SOURCE_DIR}/cmake")
find_package(faad2 REQUIRED)
find_package(easyloggingpp REQUIRED)
find_package(fmt REQUIRED)
find_package(imgui REQUIRED)
find_package(implot REQUIRED)

set(viterbi_DIR ${CMAKE_CURRENT_SOURCE_DIR}/vendor/viterbi_decoder)
find_package(viterbi CONFIG REQUIRED)

set(ARGPARSE_INSTALL OFF CACHE BOOL "Include an install target" FORCE)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/vendor/argparse)

# use prebuilt binaries for windows otherwise pkgconfig
if(WIN32) 
    set(rtlsdr_DIR ${CMAKE_CURRENT_SOURCE_DIR}/vendor/librtlsdr)
    find_package(rtlsdr CONFIG REQUIRED)
    set(RTLSDR_LIBS rtlsdr::rtlsdr)
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(rtlsdr REQUIRED IMPORTED_TARGET librtlsdr)
    set(RTLSDR_LIBS PkgConfig::rtlsdr)
endif()

# use vcpkg for windows otherwise pkgconfig
if(WIN32)
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

# for posix threads
if(NOT WIN32) 
    find_package(Threads REQUIRED)
    add_compile_options(-pthread) 
    link_libraries(Threads::Threads)
endif()

# setup easyloggingpp
target_compile_definitions(easyloggingpp PRIVATE ELPP_THREAD_SAFE)
set(DAB_CORE_USE_EASYLOGGING ON CACHE BOOL "Use easylogging for dab_core" FORCE)
set(BASIC_RADIO_USE_EASYLOGGING ON CACHE BOOL "Use easylogging for basic_radio" FORCE)
set(BASIC_SCRAPER_USE_EASYLOGGING ON CACHE BOOL "Use easylogging for basic_scraper" FORCE)

add_subdirectory(${CMAKE_SOURCE_DIR}/src)
add_subdirectory(${CMAKE_SOURCE_DIR}/examples)