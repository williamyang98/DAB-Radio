#!/bin/sh
sudo apt-get --yes install build-essential ninja-build
sudo apt-get --yes install qemu-user qemu-user-static gcc-aarch64-linux-gnu g++-aarch64-linux-gnu binutils-aarch64-linux-gnu binutils-aarch64-linux-gnu-dbg
sudo dpkg --add-architecture arm64
sudo cat ./toolchains/arm/sources.list | sudo tee -a /etc/apt/sources.list
sudo apt-get --yes update

# We need to install these manually for libglfw3 since apt doesn't seem to want to automatically install these
sudo apt-get --yes install libllvm15:arm64
sudo apt-get --yes install libgl1-mesa-dri:arm64
sudo apt-get --yes install libglfw3-dev:arm64 libopengl-dev:arm64 
sudo apt-get --yes install libportaudio2:arm64 portaudio19-dev:arm64 librtlsdr-dev:arm64 libfftw3-dev:arm64 libfftw3-single3:arm64