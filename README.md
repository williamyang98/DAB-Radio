# Introduction
[![x86-windows](https://github.com/williamyang98/DAB-Radio/actions/workflows/x86-windows.yml/badge.svg)](https://github.com/williamyang98/DAB-Radio/actions/workflows/x86-windows.yml)
[![x86-linux](https://github.com/williamyang98/DAB-Radio/actions/workflows/x86-linux.yml/badge.svg)](https://github.com/williamyang98/DAB-Radio/actions/workflows/x86-linux.yml)
[![x86-macos](https://github.com/williamyang98/DAB-Radio/actions/workflows/x86-macos.yml/badge.svg)](https://github.com/williamyang98/DAB-Radio/actions/workflows/x86-macos.yml)

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
| src/basic_scraper | Listens to basic_radio instance to save audio/slideshow/MOT data to disk |
| examples/*.cpp    | All our sample applications |

# Gallery
![OFDM Demodulator GUI](docs/gallery/ofdm_demodulator_gui.png)
![Simple Radio GUI](docs/gallery/simple_radio_gui.png)

# Download and run (Windows)
1. Download the ZIP archive from the releases page. 
2. Setup rtlsdr radio drivers according to [here](https://www.rtl-sdr.com/rtl-sdr-quick-start-guide/)
3. Plug in your RTLSDR Blog v3 dongle
4. Run ```./radio_app.exe```
5. Go to the "Simple View" tab and select a service from the list. 
6. Click "Run All" to listen to the channel and receive slideshows.

- Refer to [Wohnort](http://www.wohnort.org/dab/) for DAB ensembles in your region.
- Read ```examples/README.md``` to use other applications.

If you can't find any DAB ensembles in your area, then you can download binary files from the Releases page [here](https://github.com/williamyang98/DAB-Radio/releases/tag/raw-iq-data). These contain raw IQ values as well as pre-demodulated OFDM digital frames. You can read in these files with the applications described in ```examples/README.md```

# Building programs
- Refer to ```toolchains/*/README.md``` to build for your platform.
- The continuous integration (CI) scripts are in ```.github/workflows``` if you want to replicate the build on your system.
- SIMD instructions are used for x86 and ARM cpus to speed up math heavy code paths. Modify ```CMakePresets.json``` to use correct compiler flags.

# Similar apps
- The welle.io open source radio has an excellent implementation of DAB radio. Their repository can be found [here](https://github.com/albrechtl/welle.io). [Youtube Link](https://www.youtube.com/watch?v=IJcgdmud-AI). 
- There is a large community of rtl-sdr projects which can be found at [rtl-sdr.com](https://www.rtl-sdr.com/tag/dab/). This link points to a webpage showcasing several open source community projects that aim to decode DAB signals.

# Important sources
- [ETSI](https://www.etsi.org/standards) the non-for-profit standardisation organisation for making all of the standards free to access and view. Without their detailed documentation and specifications it would not be possible to build a rtl-sdr DAB radio.
- [Phil Karn](https://github.com/ka9q) for his Reed Solomon and Viterbi decoding algorithms which can be found [here](https://github.com/ka9q/libfec)
- [tcbrindle](https://github.com/tcbrindle) for his C++ single header template library implementation of std::span which can be found [here](https://github.com/tcbrindle/span)

# TODO
- For DAB+ determine how to perform error correction on the firecode CRC16 in the AAC super frame.
- Add TII (transmitter identificaton information) decoding
