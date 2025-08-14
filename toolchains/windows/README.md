## Instructions
1. Install vcpkg and integrate install. Refer to instructions [here](https://github.com/microsoft/vcpkg#quick-start-windows)
2. Setup x64 developer environment for C++ for MSVC.
3. Configure cmake: ```cmake . -B build --preset windows-msvc -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE={VCPKG_ROOT_DIR}\scripts\buildsystems\vcpkg.cmake```
    - Change ```-DCMAKE_TOOLCHAIN_FILE``` to point to your vcpkg installation directory.
4. Build: ```cmake --build build --config Release```
5. Run: ```.\build\examples\*.exe```

## Compiling with clang-cl
Compiling with clang gives better performing binaries compared to MSVC.
1. Make sure clang for C++ is installed through Visual Studio.
2. Configure cmake: ```cmake . -B build --preset clang -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=C:\tools\vcpkg\scripts\buildsystems\vcpkg.cmake```

## Compiling with different SIMD flags
Compile with a different level of SIMD support by changing the preset when configuring cmake (step 3). Do this if you want to compile for older machines which may not support AVX or AVX2 instructions.

| Preset | Instruction set |
| --- | --- |
| windows-msvc | default |
| windows-msvc-sse2 | sse2 |
| windows-msvc-avx | avx |
| windows-msvc-avx2 | avx2 |

*(Optional)* FFTW3 is built by default with AVX2 instructions. Modify ```vcpkg.json``` so fftw3 uses the correct "features" for your CPU.
- ```"features": ["avx"]```
- ```"features": ["sse2"]``` 
- ```"features": ["sse"]```

## Updating vcpkg
Update vcpkg if URLs or mirrors become outdated (not sure if there is a better way of automating this)
- [Getting started guide for vcpkg](https://learn.microsoft.com/en-us/vcpkg/get_started/get-started?pivots=shell-powershell#1---set-up-vcpkg)

Go into your vcpkg install:
1. Pull latest commit: ```git pull```
2. Bootstrap install: ```./bootstrap-vcpkg.sh```

Bump the vcpkg baseline commitish in:
- ```vcpkg.json```
- ```.github/workflows/x86-windows.yml``` for CI builds
