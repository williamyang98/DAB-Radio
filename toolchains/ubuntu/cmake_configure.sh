#!/bin/sh
cmake . -B build-ubuntu -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=./toolchains/ubuntu/linux-gnu-toolchain.cmake