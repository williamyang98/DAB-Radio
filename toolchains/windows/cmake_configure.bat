@echo off

set BUILD_TYPE="Release"
set TOOLCHAIN_FILE="C:/tools/vcpkg/scripts/buildsystems/vcpkg.cmake"
set BUILD_OUT="build-windows"
set BUILD_GENERATOR="Visual Studio 17 2022"
set ARCHITECTURE="x64"

@echo on

call cmake^
 -B %BUILD_OUT%^
 -DCMAKE_BUILD_TYPE:STRING=%BUILD_TYPE%^
 -DCMAKE_TOOLCHAIN_FILE:STRING=%TOOLCHAIN_FILE%^
 -DCMAKE_EXPORT_COMPILE_COMMANDS:BOOL=TRUE^
 -G %BUILD_GENERATOR%^
 -A %ARCHITECTURE%
