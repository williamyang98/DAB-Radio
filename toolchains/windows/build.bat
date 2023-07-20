@echo off

set PROJ_PATH="%2"
if "%~2" == "" set PROJ_PATH="build-windows\ALL_BUILD.vcxproj" 

set BUILD_REF="true"
if "%3" == "--ignore-ref" set BUILD_REF="false" 

set ARGS=^
 /p:CL_MPCOUNT=%NUMBER_OF_PROCESSORS%^
 /m:%NUMBER_OF_PROCESSORS%^
 /p:BuildProjectReferences=%BUILD_REF%^
 /v:minimal^
 /nologo 

if /I "%1" == "" (
    set BUILD_TYPE="Release"
    goto BUILD
)
if /I "%1" == "release" (
    set BUILD_TYPE="Release"
    goto BUILD
)
if /I "%1" == "debug" (
    set BUILD_TYPE="Debug"
    goto BUILD
)
if /I "%1" == "reldeb" (
    set BUILD_TYPE="RelWithDebInfo"
    goto BUILD
)
if /I "%1" == "minsize" (
    set BUILD_TYPE="MinSizeRel"
    goto BUILD
)
goto ERROR


:BUILD
@echo on
call msbuild /p:Configuration=%BUILD_TYPE% %PROJ_PATH% %ARGS%
@echo off
goto EXIT


:ERROR
echo Invalid build type: "%1"
goto EXIT

:EXIT
