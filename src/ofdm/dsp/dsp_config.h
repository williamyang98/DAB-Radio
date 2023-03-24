#pragma once

// Indicate widest intrinsic available
#if defined(__AVX2__)
#pragma message("Compiling OFDM DSP using AVX2 code")
#elif defined(__SSSE3__)
#pragma message("Compiling OFDM DSP using SSSE3 code")
#else
#pragma message("Compiling OFDM DSP using scalar code")
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
#pragma message("Compiling OFDM DSP with FMA instructions")
#endif