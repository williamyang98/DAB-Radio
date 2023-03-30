#pragma once

// Determine the architecture of the CPU we are compiling for so
// we can decide on the best SIMD vectorisation to select
// Supported architecture detection:
// __ARCH_X86__
// __ARCH_AARCH64__
// __ARCH_ANY__

#if defined(MSVC) || defined(_MSC_VER)
    #if defined(_M_IX86) || defined(_M_X64) || defined(_M_AMD64)
        #define __ARCH_X86__
    #else
        #define __ARCH_ANY__
    #endif
#elif defined(GCC) || defined(__GNUC__) || defined(__MINGW32__) || defined(__MINGW64__)
    #if defined(__i386__) || defined(__x86_64__) || defined(__amd64__)
        #define __ARCH_X86__
    #elif defined(__aarch64__)
        #define __ARCH_AARCH64__
    #else
        #define __ARCH_ANY__
    #endif
#else
    #define __ARCH_ANY__
#endif