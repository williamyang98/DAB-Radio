## Instructions
1. Install vcpkg and integrate install. Refer to instructions [here](https://github.com/microsoft/vcpkg#quick-start-windows)
2. Setup x64 developer environment for C++ for MSVC.
2. Configure cmake: ```cmake . -B build --preset windows-msvc -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=C:\tools\vcpkg\scripts\buildsystems\vcpkg.cmake```
    - Change ```-DCMAKE_TOOLCHAIN_FILE``` to point to your vcpkg installation directory.
3. Build: ```cmake --build build --config Release```
4. Run: ```.\build\examples\*.exe```

## Compiling with clang-cl
Compiling with clang gives better performing binaries compared to MSVC.
1. Make sure clang for C++ is installed through Visual Studio.
2. Configure cmake: ```cmake . -B build --preset clang -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=C:\tools\vcpkg\scripts\buildsystems\vcpkg.cmake```

## Compiling with different SIMD flags
The default windows build compiles with AVX2 instructions. If your CPU doesn't support these then you will have to modify the scripts. 

Modify ```CMakePresets.json``` so that configurePresets ```windows-msvc``` uses correct flags. [Link to docs](https://learn.microsoft.com/en-us/cpp/build/reference/arch-x64?view=msvc-170)
- ```/arch:AVX2```
- ```/arch:AVX```
- Remove the ```/arch:XXX``` flag entirely for SSE2 builds

*(Optional)* FFTW3 is built by default with AVX2 instructions. Modify ```vcpkg.json``` so fftw3 uses the correct "features" for your CPU. This might not be necessary since it uses ```cpu_features``` to determine and dispatch calls at runtime.
- ```"features": ["avx"]```
- ```"features": ["sse2"]``` 
- ```"features": ["sse"]```
