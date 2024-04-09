#pragma once

#include <stdint.h>
#include "simd_flags.h" // NOLINT

// Multiply packed complex float 

#if defined(__AVX__)
#include <immintrin.h>
static inline __m256 c32_mul_avx(__m256 x0, __m256 x1) {
    // Vectorise complex multiplication
    // [3 2 1 0] -> [2 3 0 1]
    constexpr uint8_t SWAP_COMPONENT_MASK = 0b10110001;
    // [3 2 1 0] -> [2 2 0 0]
    constexpr uint8_t GET_REAL_MASK = 0b10100000;
    // [3 2 1 0] -> [3 3 1 1]
    constexpr uint8_t GET_IMAG_MASK = 0b11110101;

    // [d c]
    __m256 a0 = _mm256_permute_ps(x0, SWAP_COMPONENT_MASK);
    // [a a]
    __m256 a1 = _mm256_permute_ps(x1, GET_REAL_MASK);
    // [b b]
    __m256 a2 = _mm256_permute_ps(x1, GET_IMAG_MASK);
    // [bd bc]
    __m256 b0 = _mm256_mul_ps(a2, a0);

    #if !defined(__FMA__)
    // [ac ad]
    __m256 b1 = _mm256_mul_ps(a1, x0);
    // [ac-bd ad+bc]
    __m256 y = _mm256_addsub_ps(b1, b0);
    #else
    // [ac-bd ad+bc]
    __m256 y = _mm256_fmaddsub_ps(a1, x0, b0);
    #endif

    return y;
}
#endif

#if defined(__SSE3__)
#include <xmmintrin.h>
static inline __m128 c32_mul_sse3(__m128 x0, __m128 x1) {
    // Vectorise complex multiplication
    // [3 2 1 0] -> [2 3 0 1]
    constexpr uint8_t SWAP_COMPONENT_MASK = 0b10110001;
    // [3 2 1 0] -> [2 2 0 0]
    constexpr uint8_t GET_REAL_MASK = 0b10100000;
    // [3 2 1 0] -> [3 3 1 1]
    constexpr uint8_t GET_IMAG_MASK = 0b11110101;

    // [d c]
    __m128 a0 = _mm_shuffle_ps(x1, x1, SWAP_COMPONENT_MASK);
    // [a a]
    __m128 a1 = _mm_shuffle_ps(x0, x0, GET_REAL_MASK);
    // [b b]
    __m128 a2 = _mm_shuffle_ps(x0, x0, GET_IMAG_MASK);
    // [bd bc]
    __m128 b0 = _mm_mul_ps(a2, a0);

    #if !defined(__FMA__)
    // [ac ad]
    __m128 b1 = _mm_mul_ps(a1, x1);
    // [ac-bd ad+bc]
    __m128 y = _mm_addsub_ps(b1, b0);
    #else
    // [ac-bd ad+bc]
    __m128 y = _mm_fmaddsub_ps(a1, x1, b0);
    #endif

    return y;
}
#endif