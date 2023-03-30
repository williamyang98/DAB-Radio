#!/bin/sh
sudo apt-get --yes install build-essential ninja-build
sudo apt-get --yes install qemu-user qemu-user-static gcc-aarch64-linux-gnu g++-aarch64-linux-gnu binutils-aarch64-linux-gnu binutils-aarch64-linux-gnu-dbg
sudo dpkg --add-architecture arm64
sudo cat ./toolchains/arm/sources.list | sudo tee -a /etc/apt/sources.list
sudo apt-get --yes update
sudo apt-get --yes install libglfw3-dev:arm64 libopengl-dev:arm64 libportaudio2:arm64 portaudio19-dev:arm64 librtlsdr-dev:arm64 libfftw3-dev:arm64 libfftw3-single3:arm64