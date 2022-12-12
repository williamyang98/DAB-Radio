#include "apply_pll.h"

#define _USE_MATH_DEFINES
#include <math.h>
#include <complex>
#include <assert.h>

// NOTE: Fixed rate sample rate for OFDM
const float Ts = 1.0f/2.048e6f;

float apply_pll_scalar(
    tcb::span<const std::complex<float>> x0, 
    tcb::span<std::complex<float>> y, 
    const float freq_offset,
    const float dt0)
{
    assert(x0.size() == y.size());
    const auto N = x0.size();
    const float dt_step = 2.0f * (float)M_PI * freq_offset * Ts;
    const bool is_large_offset = std::abs(freq_offset) > 1500.0f;

    float dt = dt0;
    for (int i = 0; i < N; i++) {
        const auto pll = std::complex<float>(
            std::cos(dt),
            std::sin(dt));

        y[i] = x0[i] * pll;
        dt += dt_step;
        if (is_large_offset) {
            dt = std::fmod(dt, 2.0f*(float)M_PI);
        }
    }
    return dt;
}

// TODO: Modify code to support ARM platforms like Raspberry PI using NEON
#include <immintrin.h>

#ifdef _MSC_VER
#define ALIGNED(x) __declspec(align(x))
#else
#define ALIGNED(x) __attribute__ ((aligned(x)))
#endif

// NOTE: On GCC we do not have the Intel SVML library 
//       Only visual studio 2019 onwards has it
//       This means _mm_cos_ps and _mm256_cos_ps are missing
#ifndef _MSC_VER
#pragma message("Compiling PLL with external Intel SVML library")

#if defined(_OFDM_DSP_SSSE3)
#define SSE_MATHFUN_WITH_CODE
#include "sse_mathfun.h"
#define _mm_cos_ps(x) cos_ps(x)
#endif

#if defined(_OFDM_DSP_AVX2)
#include "avx_mathfun.h"
#define _mm256_cos_ps(x) cos256_ps(x)
#endif
#endif

// Helper unions for using floating point and integer SIMDs
#if defined(_OFDM_DSP_AVX2)
typedef union ALIGNED(sizeof(__m256)) cpx256_t {
    float f32[8];
    std::complex<float> c32[4];
    __m128 m128[2];
    __m256 ps;
    __m256i i;
    cpx256_t() {}
} cpx256_t;
#endif

#if defined(_OFDM_DSP_SSSE3)
typedef union ALIGNED(sizeof(__m128)) cpx128_t {
    float f32[4];
    std::complex<float> c32[2];
    __m128 ps;
    __m128i i;
    cpx128_t() {}
} cpx128_t;
#endif

// Manual AVX2 code which is up to 4x faster
#if defined(_OFDM_DSP_AVX2)
float apply_pll_avx2(
    tcb::span<const std::complex<float>> x0, 
    tcb::span<std::complex<float>> y, 
    const float freq_offset,
    const float dt0) 
{
    assert(x0.size() == y.size());
    const auto N = x0.size();
    const float dt_step = 2.0f * (float)M_PI * freq_offset * Ts;
    const bool is_large_offset = std::abs(freq_offset) > 1500.0f;

    // 256bits = 32bytes = 4*8bytes
    const int K = 4;
    const auto M = N/K;

    // Vectorise calculation of cos(dt) + jsin(dt)
    cpx256_t dt_step_pack;
    const float dt_step_pack_stride = dt_step * K;
    {
        float x = 0.0f;
        for (int i = 0; i < K; i++) {
            // cos(dt)
            dt_step_pack.f32[2*i+0] = x;
            // cos(dt-pi/2) = sin(dt)
            dt_step_pack.f32[2*i+1] = x - ((float)M_PI / 2.0f);
            x += dt_step;
        }
    }

    // Vectorise complex multiplication
    // [3 2 1 0] -> [2 3 0 1]
    const uint8_t SWAP_COMPONENT_MASK = 0b10110001;
    // [3 2 1 0] -> [2 2 0 0]
    const uint8_t GET_REAL_MASK = 0b10100000;
    // [3 2 1 0] -> [3 3 1 1]
    const uint8_t GET_IMAG_MASK = 0b11110101;

    float dt = dt0;
    for (int i = 0; i < M; i++) {
        // Vectorised cos(dt) + jsin(dt)
        __m256 dt_pack = _mm256_set1_ps(dt);
        dt_pack = _mm256_add_ps(dt_pack, dt_step_pack.ps);
        dt += dt_step_pack_stride;
        if (is_large_offset) {
            dt = std::fmod(dt, 2.0f*(float)M_PI);
        }
        __m256 pll = _mm256_cos_ps(dt_pack);

        // Perform vectorised complex multiplication
        // NOTE: Use unaligned load
        __m256 X = _mm256_loadu_ps(reinterpret_cast<const float*>(&x0[i*K]));

        // [d c]
        __m256 a0 = _mm256_permute_ps(pll, SWAP_COMPONENT_MASK);
        // [a a]
        __m256 a1 = _mm256_permute_ps(X, GET_REAL_MASK);
        // [b b]
        __m256 a2 = _mm256_permute_ps(X, GET_IMAG_MASK);
        // [bd bc]
        __m256 b0 = _mm256_mul_ps(a2, a0);

        #if !defined(_OFDM_DSP_FMA)
        // [ac ad]
        __m256 b1 = _mm256_mul_ps(a1, pll);
        // [ac-bd ad+bc]
        __m256 Y = _mm256_addsub_ps(b1, b0);
        #else
        // [ac-bd ad+bc]
        __m256 Y = _mm256_fmaddsub_ps(a1, pll, b0);
        #endif

        // NOTE: Use unaligned store
        _mm256_storeu_ps(reinterpret_cast<float*>(&y[i*K]), Y);
    }

    const size_t N_vector = M*K;
    dt = apply_pll_scalar(x0.subspan(N_vector), y.subspan(N_vector), freq_offset, dt);

    return dt;
}
#endif

// Manual SSSE3 code which is up to 2x 
#if defined(_OFDM_DSP_SSSE3)
float apply_pll_ssse3(
    tcb::span<const std::complex<float>> x0, 
    tcb::span<std::complex<float>> y, 
    const float freq_offset,
    const float dt0) 
{
    assert(x0.size() == y.size());
    const auto N = x0.size();
    const float dt_step = 2.0f * (float)M_PI * freq_offset * Ts;
    const bool is_large_offset = std::abs(freq_offset) > 1500.0f;

    // 128bits = 16bytes = 2*8bytes
    const int K = 2;
    const auto M = N/K;

    // Vectorise calculation of cos(dt) + jsin(dt)
    cpx128_t dt_step_pack;
    const float dt_step_pack_stride = dt_step * K;
    {
        float x = 0.0f;
        for (int i = 0; i < K; i++) {
            // cos(dt)
            dt_step_pack.f32[2*i+0] = x;
            // cos(dt-pi/2) = sin(dt)
            dt_step_pack.f32[2*i+1] = x - ((float)M_PI / 2.0f);
            x += dt_step;
        }
    }

    // Vectorise complex multiplication
    // NOTE: For SSE3 we use _mm_shuffle_ps(a, a, MASK) instead of _mm_permute_ps(a, MASK)
    //       This is because _mm_permute_ps is a AVX intrinsic
    // [3 2 1 0] -> [2 3 0 1]
    const uint8_t SWAP_COMPONENT_MASK = 0b10110001;
    // [3 2 1 0] -> [2 2 0 0]
    const uint8_t GET_REAL_MASK = 0b10100000;
    // [3 2 1 0] -> [3 3 1 1]
    const uint8_t GET_IMAG_MASK = 0b11110101;

    float dt = dt0;
    for (int i = 0; i < M; i++) {
        // Vectorised cos(dt) + jsin(dt)
        __m128 dt_pack = _mm_set1_ps(dt);
        dt_pack = _mm_add_ps(dt_pack, dt_step_pack.ps);
        dt += dt_step_pack_stride;
        if (is_large_offset) {
            dt = std::fmod(dt, 2.0f*(float)M_PI);
        }
        __m128 pll = _mm_cos_ps(dt_pack);

        // Perform vectorised complex multiplication
        // NOTE: Use unaligned load
        __m128 X = _mm_loadu_ps(reinterpret_cast<const float*>(&x0[i*K]));

        // [d c]
        __m128 a0 = _mm_shuffle_ps(pll, pll, SWAP_COMPONENT_MASK);
        // [a a]
        __m128 a1 = _mm_shuffle_ps(X, X, GET_REAL_MASK);
        // [b b]
        __m128 a2 = _mm_shuffle_ps(X, X, GET_IMAG_MASK);
        // [bd bc]
        __m128 b0 = _mm_mul_ps(a2, a0);

        #if !defined(_OFDM_DSP_FMA)
        // [ac ad]
        __m128 b1 = _mm_mul_ps(a1, pll);
        // [ac-bd ad+bc]
        __m128 Y = _mm_addsub_ps(b1, b0);
        #else
        // [ac-bd ad+bc]
        __m128 Y = _mm_fmaddsub_ps(a1, pll, b0);
        #endif

        // NOTE: Use unaligned store
        _mm_storeu_ps(reinterpret_cast<float*>(&y[i*K]), Y);
    }

    const size_t N_vector = M*K;
    dt = apply_pll_scalar(x0.subspan(N_vector), y.subspan(N_vector), freq_offset, dt);

    return dt;
}
#endif
