# Introduction
[![x86-windows](https://github.com/FiendChain/DAB-Radio/actions/workflows/x86-windows.yml/badge.svg)](https://github.com/FiendChain/DAB-Radio/actions/workflows/x86-windows.yml)
[![x86-linux](https://github.com/FiendChain/DAB-Radio/actions/workflows/x86-linux.yml/badge.svg)](https://github.com/FiendChain/DAB-Radio/actions/workflows/x86-linux.yml)
[![arm-linux](https://github.com/FiendChain/DAB-Radio/actions/workflows/arm-linux.yml/badge.svg)](https://github.com/FiendChain/DAB-Radio/actions/workflows/arm-linux.yml)

An implementation of a DAB (digital audio broadcasting) radio using software defined radio. 

For a description of what software defined radio is refer to this [link](https://www.rtl-sdr.com/about-rtl-sdr/). 

[![Decoding DAB radio using SDR](http://img.youtube.com/vi/4bb0FQFrgE8/0.jpg)](http://youtu.be/4bb0FQFrgE8 "Decoding DAB radio using SDR")

This repository contains applications that:
1. Demodulate the OFDM (orthogonal frequency division multiplexed) raw IQ signals into a digital frame
2. Decode DAB digital OFDM frames for use into a radio application

For those who are interested only in parts of the implementation refer to the following directories:

| Directory | Description |
| --- | --- |
| src/ofdm          | OFDM demodulation code |
| src/dab           | DAB digital decoding core algorithms |
| src/basic_radio   | Combines all of the DAB core algorithms into a cohesive example app |
| src/basic_scraper | Attaches itself via callbacks to a basic_radio instance to save audio/slideshow/MOT data to disk |
| src/utility       | Small helper template library for general purpose use |
| examples/*.cpp    | All our sample applications |

# Gallery
![OFDM Demodulator GUI](docs/gallery/ofdm_demodulator_gui.png)
![Simple Radio GUI](docs/gallery/simple_radio_gui.png)

# Download and run
1. Download the ZIP archive from the releases page. 
2. Setup rtlsdr radio drivers according to [here](https://www.rtl-sdr.com/rtl-sdr-quick-start-guide/)
3. Plug in your RTLSDR Blog v3 dongle
4. Run <code>./radio_app.exe</code>
5. Go to the simple_view tab and select a service from the list. 
6. Click "Run All" to listen to the channel and receive slideshows.

[Wohnort](http://www.wohnort.org/dab/) has an excellent website for viewing the list of DAB ensembles across the work. In Australia where I am, the blocks being used in Sydney are <code>[9A,9B,9C]</code>.

Refer to <code>src/examples/README.md</code> for other example applications.

If you can't find any DAB ensembles in your area, then you can download binary files from the Releases page [here](https://github.com/FiendChain/DAB-Radio/releases/tag/raw-iq-data). These contain raw IQ values as well as pre-demodulated OFDM digital frames. You can read in these files with the applications described in <code>src/examples/README.md</code>

# Building programs
Clone the repository using the command

<code>git clone https://github.com/FiendChain/DAB-Radio.git --recurse-submodules -j8</code>

## Method 1. Windows (cmd)
Windows build system requires:
- Visual Studio C++ SDK.
- [vcpkg](https://vcpkg.io/en/getting-started.html)

1. Setup 64bit environment by running <code>vcvars64.bat</code> from C++ sdk
2. Change directory to root of this directory
3. Run <code>fx cmake-conf</code> to configure cmake
4. Run <code>fx build release build\ALL_BUILD.vcxproj</code> to build all applications

***NOTE***: Modify the fx.bat helper batch file to point at your vcpkg install directory. 

## Method 2. Ubuntu 22.04
Refer to instructions in <code>toolchains/x86/README.md</code>

## Build notes (Read this if you get illegal instructions)
The core algorithms for OFDM demodulation and DAB digital decoding are platform independent. The GUI code uses imgui which works on Windows and Ubuntu. 

Vector instructions are used for x86 and ARM cpus to speed up parts of the code.
You can change the compiler options in CMakeLists.txt to disable or enable these to suit your CPU.

Refer to [this github issue](https://github.com/FiendChain/DAB-Radio/issues/2#issuecomment-1627787907) explaining how to modify the build for **older CPUs**.

***NOTE***: On Windows you also need to edit the <code>vcpkg.json</code> manifest so that FFTW uses the correct feature instead of "avx2". Valid options are ["avx2","avx","sse2","sse"]. Linux builds will compile to the cpu's microarchitecture using <code>-march=native</code>.

Dependencies are (refer to <code>vcpkg.json</code> or <code>toolchains/*/install_packages.sh</code>):
- glfw3
- opengl
- portaudio
- fftw3

The continuous integration (CI) scripts are in <code>.github/workflows</code> if you want to replicate the build on your system.

# Inspirations
- The welle.io open source radio has an excellent implementation of DAB radio. Their implementation is much more featureful and optimised than mine. Their repository can be found [here](https://github.com/albrechtl/welle.io). They also have a youtube video showcasing their wonderful software [here](https://www.youtube.com/watch?v=IJcgdmud-AI). 

- There is a large community of rtl-sdr projects which can be found at [rtl-sdr.com](https://www.rtl-sdr.com/tag/dab/). This link points to a webpage showcasing several open source community projects that aim to decode DAB signals.

# Important sources
- [ETSI](https://www.etsi.org/standards) the non-for-profit standardisation organisation for making all of the standards free to access and view. Without their detailed documentation and specifications it would not be possible to build a rtl-sdr DAB radio.
- [Phil Karn](https://github.com/ka9q) for his Reed Solomon and Viterbi decoding algorithms which can be found [here](https://github.com/ka9q/libfec)
- [tcbrindle](https://github.com/tcbrindle) for his C++ single header template library implementation of std::span which can be found [here](https://github.com/tcbrindle/span)
- [reyoung/avx_mathfun](https://github.com/reyoung/avx_mathfun) for their AVX/AVX2 implementations of _mm512_cos_pd
- [RJVB/sse_mathfun](https://github.com/RJVB/sse_mathfun) for their SSE2 implementations of _mm_cos_pd

# TODO
- For OFDM demodulator the hand written SIMD might perform extremely poorly when compiling on gcc or clang.
- For DAB+ determine how to perform error correction on the firecode CRC16 in the AAC super frame.
- Replace imgui with a retain mode alternative.
- Automatically scan DAB ensembles and channels and persistent them on the user's drive.
- Support the rest of the DAB standard.
    - MPEG-II audio for DAB channels.
    - Handle strings with utf-8, utf-16 character sets. 
- Add TII (transmitter identificaton information) decoding
- Add SNR meter
