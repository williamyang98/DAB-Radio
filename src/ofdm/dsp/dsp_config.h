#pragma once

#include "detect_architecture.h"

#if defined(__ARCH_X86__)
    #if defined(__AVX2__)
        #pragma message("OFDM_DSP using x86 AVX2 code")
    #elif defined(__SSSE3__)
        #pragma message("OFDM_DSP using x86 SSSE3 code")
    #else
        #pragma message("OFDM_DSP using x86 SCALAR code")
    #endif

    // Enable intrinsic code that can be compiled on target
    #if defined(__AVX2__)
        #define _OFDM_DSP_AVX2
    #endif

    #if defined(__AVX2__) || defined(__SSSE3__)
        #define _OFDM_DSP_SSSE3
    #endif

    // On MSVC if __AVX2__ is defined then we have FMA
    // On GCC __FMA__ is a given define
    #if !defined(__FMA__) && defined(__AVX2__)
        #define _OFDM_DSP_FMA
    #elif defined(__FMA__)
        #define _OFDM_DSP_FMA
    #endif

    #if defined(_OFDM_DSP_FMA)
        #pragma message("OFDM_DSP using x86 FMA code")
    #endif
#elif defined(__ARCH_AARCH64__)
    #define _OFDM_DSP_AARCH64
    #pragma message("OFDM_DSP using ARM AARCH64 NEON code")
#else
    #pragma message("OFDM_DSP using crossplatform SCALAR code")
#endif