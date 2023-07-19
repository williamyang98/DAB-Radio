#pragma once

#include "./detect_architecture.h"

#if defined(__ARCH_X86__)
    #if defined(_MSC_VER)
        // We do this to fix missing flags on compilers like MSVC so they match gcc and clang
        #if defined(__AVX512__) && !defined(__AVX2__)
            #define __AVX2__
        #endif
        #if defined(__AVX2__) && !defined(__AVX__)
            #define __AVX__
        #endif
        #if defined(__AVX2__) && !defined(__FMA__)
            #define __FMA__
        #endif
        #if defined(__AVX__) && !defined(__SSE4_2__)
            #define __SSE4_2__
        #endif
        #if defined(__SSE4_2__) && !defined(__SSE4_1__)
            #define __SSE4_1__
        #endif
        #if defined(__SSE4_1__) && !defined(__SSSE3__)
            #define __SSSE3__
        #endif
        #if defined(__SSSE3__) && !defined(__SSE3__)
            #define __SSE3__
        #endif
        #if defined(__SSE3__) && !defined(__SSE2__)
            #define __SSE2__
        #endif
        #if defined(__SSE2__) && !defined(__SSE__)
            #define __SSE__
        #endif
    #endif
#elif defined(__ARCH_AARCH64__)
    #define __SIMD_NEON__
#else
#endif
