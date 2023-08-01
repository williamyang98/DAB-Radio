#!/bin/sh
if [[ -z "$VCPKG_ROOT" ]]; then
    TOOLCHAIN_FILE="C:/tools/vcpkg/scripts/buildsystems/vcpkg.cmake"
else
    TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
fi

if [[ -z "$BUILD_OUT" ]]; then
    BUILD_OUT="build-windows"
fi

if [[ -z "$BUILD_TYPE" ]]; then
    BUILD_TYPE="Release"
fi

cmake .\
 -B $BUILD_OUT\
 -G Ninja\
 -DCMAKE_BUILD_TYPE:STRING=$BUILD_TYPE\
 -DCMAKE_TOOLCHAIN_FILE:STRING=$TOOLCHAIN_FILE\
 -DCMAKE_EXPORT_COMPILE_COMMANDS:BOOL=TRUE
