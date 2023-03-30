#!/bin/sh
export PKG_CONFIG_PATH=/usr/lib/aarch64-linux-gnu/pkgconfig
cmake . -B build-arm -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=./toolchains/arm/linux-gnu-toolchain.cmake