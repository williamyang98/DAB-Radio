#pragma once
#include "dsp_config.h"
#include <immintrin.h>
#include <complex>

#ifdef _MSC_VER
#define _ALIGNED(x) __declspec(align(x))
#else
#define _ALIGNED(x) __attribute__ ((aligned(x)))
#endif

// Helper unions for using floating point and integer SIMDs
#if defined(_OFDM_DSP_AVX2)
typedef union _ALIGNED(sizeof(__m256)) cpx256_t {
    float f32[8];
    std::complex<float> c32[4];
    __m128 m128[2];
    __m256 ps;
    __m256i i;
    cpx256_t() {}
} cpx256_t;
#endif

#if defined(_OFDM_DSP_SSSE3)
typedef union _ALIGNED(sizeof(__m128)) cpx128_t {
    float f32[4];
    std::complex<float> c32[2];
    __m128 ps;
    __m128i i;
    cpx128_t() {}
} cpx128_t;
#endif

#undef _ALIGNED
