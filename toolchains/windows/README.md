## Instructions
1. ```./toolchains/windows/cmake_configure.sh```
2. ```ninja -C build-windows```
3. ```./build-windows/\<program\>```

## Compiling with clang-cl
Compiling with clang gives better performing binaries compared to MSVC cl.

1. Setup x64 developer environment for C++ for MSVC.
2. Make sure clang for C++ is installed through Visual Studio.
3. Enter a bash shell.
4. ```CC=clang CXX=clang++ ./toolchains/windows/cmake_configure.sh```

## Compiling with different SIMD flags
The default windows build compiles with AVX2 instructions. If your CPU doesn't support these then you will have to modify the scripts.

```vcpkg.json``` contains ```fftw3f``` which uses the ```avx2``` feature. Change this to ```[avx, sse2, sse]```.

```CMakeLists.txt``` contains ```/arch:AVX2```. Change this to ```/arch:AVX``` or remove this compiler option.
