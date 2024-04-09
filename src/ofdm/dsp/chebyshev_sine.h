#pragma once

#include "detect_architecture.h"
#include "simd_flags.h" // NOLINT

// SOURCE: https://gist.github.com/williamyang98/7aca0ca0f1978c7374a66002892e0d8a
//         Chebyshev polynomial that approximates f(x) = sin(2*pi*x) accurately within [-0.5,+0.5]
// STEPS:  1. settings are { grad_t=double, TOTAL_COEFFICIENTS=6, SINE_ROOT=0.5, ...defaults }
//         2. train with   { coefficient_t=double, TOTAL_SAMPLES=128 }
//         3. save coefficients and use them as starting coefficients for next run
//         4. retrain with { coefficient_t=float,  TOTAL_SAMPLES=1024 }
//         5. mean absolute error should be 3.63e-8
constexpr float CHEBYSHEV_POLYNOMIAL_COEFFICIENTS[6] = {
    -25.13274193f,
     64.83583069f,
    -67.07687378f,
     38.50016403f,
    -14.07150173f,
      3.20396066f
};

static float chebyshev_sine(float x) {
    constexpr float A0 = CHEBYSHEV_POLYNOMIAL_COEFFICIENTS[0];
    constexpr float A1 = CHEBYSHEV_POLYNOMIAL_COEFFICIENTS[1];
    constexpr float A2 = CHEBYSHEV_POLYNOMIAL_COEFFICIENTS[2];
    constexpr float A3 = CHEBYSHEV_POLYNOMIAL_COEFFICIENTS[3];
    constexpr float A4 = CHEBYSHEV_POLYNOMIAL_COEFFICIENTS[4];
    constexpr float A5 = CHEBYSHEV_POLYNOMIAL_COEFFICIENTS[5];
    // Calculate g(x) = a5*x^10 + a4*x^8 + a3*x^6 + a2*x^4 + a1*x^2 + a0
    const float z = x*x;        // z = x^2
    const float b5 = A5;        // a5*z^0
    const float b4 = b5*z + A4; // a5*z^1 + a4*z^0
    const float b3 = b4*z + A3; // a5*z^2 + a4*z^1 + a3*z^0
    const float b2 = b3*z + A2; // a5*z^3 + a4*z^2 + a3*z^1 + a2*z^0
    const float b1 = b2*z + A1; // a5*z^4 + a4*z^3 + a3*z^2 + a2*z^1 + a1*z^0
    const float b0 = b1*z + A0; // a5*z^5 + a4*z^4 + a3*z^3 + a2*z^2 + a1*z^1 + a0*z^0
    // Calculate f(x) = g(x) * (x-0.5) * (x+0.5) * x
    //           f(x) = g(x) * (x^2 - 0.25) * x
    //           f(x) = g(x) * (z-0.25) * x
    return b0 * (z-0.25f) * x;
}

// x86
#if defined(__ARCH_X86__)
#include <immintrin.h>

#if defined(__SSE__)
#include <xmmintrin.h>
static inline __m128 _mm_chebyshev_sine(__m128 x) {
    const __m128 A0 = _mm_set1_ps(CHEBYSHEV_POLYNOMIAL_COEFFICIENTS[0]);
    const __m128 A1 = _mm_set1_ps(CHEBYSHEV_POLYNOMIAL_COEFFICIENTS[1]);
    const __m128 A2 = _mm_set1_ps(CHEBYSHEV_POLYNOMIAL_COEFFICIENTS[2]);
    const __m128 A3 = _mm_set1_ps(CHEBYSHEV_POLYNOMIAL_COEFFICIENTS[3]);
    const __m128 A4 = _mm_set1_ps(CHEBYSHEV_POLYNOMIAL_COEFFICIENTS[4]);
    const __m128 A5 = _mm_set1_ps(CHEBYSHEV_POLYNOMIAL_COEFFICIENTS[5]);
    // Calculate g(x) = a5*x^10 + a4*x^8 + a3*x^6 + a2*x^4 + a1*x^2 + a0
    #if defined(__FMA__)
        #define __muladd(a,b,c) _mm_fmadd_ps(a,b,c)
    #else
        #define __muladd(a,b,c) _mm_add_ps(_mm_mul_ps(a,b),c)
    #endif
    const __m128 z = _mm_mul_ps(x,x);    // z = x^2
    const __m128 b5 = A5;                // a5*z^0
    const __m128 b4 = __muladd(b5,z,A4); // a5*z^1 + a4*z^0
    const __m128 b3 = __muladd(b4,z,A3); // a5*z^2 + a4*z^1 + a3*z^0
    const __m128 b2 = __muladd(b3,z,A2); // a5*z^3 + a4*z^2 + a3*z^1 + a2*z^0
    const __m128 b1 = __muladd(b2,z,A1); // a5*z^4 + a4*z^3 + a3*z^2 + a2*z^1 + a1*z^0
    const __m128 b0 = __muladd(b1,z,A0); // a5*z^5 + a4*z^4 + a3*z^3 + a2*z^2 + a1*z^1 + a0*z^0
    #undef __muladd
    // Calculate f(x) = g(x) * (x-0.5) * (x+0.5) * x
    //           f(x) = g(x) * (x^2 - 0.25) * x
    //           f(x) = g(x) * (z-0.25) * x
    const __m128 c0 = _mm_sub_ps(z,_mm_set1_ps(0.25f));
    return _mm_mul_ps(_mm_mul_ps(b0,c0),x);
}
#endif

#if defined(__AVX__)
static inline __m256 _mm256_chebyshev_sine(__m256 x) {
    const __m256 A0 = _mm256_set1_ps(CHEBYSHEV_POLYNOMIAL_COEFFICIENTS[0]);
    const __m256 A1 = _mm256_set1_ps(CHEBYSHEV_POLYNOMIAL_COEFFICIENTS[1]);
    const __m256 A2 = _mm256_set1_ps(CHEBYSHEV_POLYNOMIAL_COEFFICIENTS[2]);
    const __m256 A3 = _mm256_set1_ps(CHEBYSHEV_POLYNOMIAL_COEFFICIENTS[3]);
    const __m256 A4 = _mm256_set1_ps(CHEBYSHEV_POLYNOMIAL_COEFFICIENTS[4]);
    const __m256 A5 = _mm256_set1_ps(CHEBYSHEV_POLYNOMIAL_COEFFICIENTS[5]);
    // Calculate g(x) = a5*x^10 + a4*x^8 + a3*x^6 + a2*x^4 + a1*x^2 + a0
    #if defined(__FMA__)
        #define __muladd(a,b,c) _mm256_fmadd_ps(a,b,c)
    #else
        #define __muladd(a,b,c) _mm256_add_ps(_mm256_mul_ps(a,b),c)
    #endif
    const __m256 z = _mm256_mul_ps(x,x); // z = x^2
    const __m256 b5 = A5;                // a5*z^0
    const __m256 b4 = __muladd(b5,z,A4); // a5*z^1 + a4*z^0
    const __m256 b3 = __muladd(b4,z,A3); // a5*z^2 + a4*z^1 + a3*z^0
    const __m256 b2 = __muladd(b3,z,A2); // a5*z^3 + a4*z^2 + a3*z^1 + a2*z^0
    const __m256 b1 = __muladd(b2,z,A1); // a5*z^4 + a4*z^3 + a3*z^2 + a2*z^1 + a1*z^0
    const __m256 b0 = __muladd(b1,z,A0); // a5*z^5 + a4*z^4 + a3*z^3 + a2*z^2 + a1*z^1 + a0*z^0
    #undef __muladd
    // Calculate f(x) = g(x) * (x-0.5) * (x+0.5) * x
    //           f(x) = g(x) * (x^2 - 0.25) * x
    //           f(x) = g(x) * (z-0.25) * x
    const __m256 c0 = _mm256_sub_ps(z,_mm256_set1_ps(0.25f));
    return _mm256_mul_ps(_mm256_mul_ps(b0,c0),x);
}
#endif

#endif
