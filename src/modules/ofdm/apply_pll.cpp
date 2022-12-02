#include "apply_pll.h"

#define _USE_MATH_DEFINES
#include <math.h>
#include <complex>
#include <string.h> // memcpy
#include "utility/span.h"
#include <immintrin.h>

const float Ts = 1.0f/2.048e6f;

#ifdef _MSC_VER
#define ALIGNED(x) __declspec(align(x))
#else
#define ALIGNED(x) __attribute__ ((aligned(x)))
#endif

// NOTE: On GCC we do not have the Intel SVML library 
//       Only visual studio 2019 onwards has it
//       This means _mm_cos_ps and _mm256_cos_ps are missing
#ifndef _MSC_VER
#pragma message("Using external Intel SVML library")

#if defined(__SSSE3__)
#define SSE_MATHFUN_WITH_CODE
#include "sse_mathfun.h"
#define _mm_cos_ps(x) cos_ps(x)
#endif

#if defined(__AVX2__)
#include "avx_mathfun.h"
#define _mm256_cos_ps(x) cos256_ps(x)
#endif
#endif

// Helper function for using floating point and integer SIMDs
typedef union ALIGNED(sizeof(__m256)) {
    float f32[8];
    __m256 ps;
    __m256i i;
} cpx256_t;

typedef union ALIGNED(sizeof(__m128)) {
    float f32[4];
    __m128 ps;
    __m128i i;
} cpx128_t;

// Generally the compiler will use SSE4 to vectorise sincos
float apply_pll_scalar(
    tcb::span<const std::complex<float>> x0, 
    tcb::span<std::complex<float>> y, 
    const float freq_offset,
    const float dt0)
{
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

// Manual AVX2 code which is up to 4x faster
#if defined(__AVX2__)
float apply_pll_avx2(
    tcb::span<const std::complex<float>> x0, 
    tcb::span<std::complex<float>> y, 
    const float freq_offset,
    const float dt0) 
{
    const auto N = x0.size();
    const float dt_step = 2.0f * (float)M_PI * freq_offset * Ts;
    const bool is_large_offset = std::abs(freq_offset) > 1500.0f;

    // 256bits = 32bytes = 4*8bytes
    const int K = 4;
    const auto M = N/K;

    // Vectorise calculation of cos(dt) + jsin(dt)
    __m256 dt_pack;
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
    // We need to do component wise multiplication
    // This is done using:
    // 1. Shifting of the 256bit array
    // 2. Masking of the 256bit array
    cpx256_t real_mask, imag_mask;
    real_mask.i = _mm256_set1_epi64x(0x00000000FFFFFFFF);
    imag_mask.i = _mm256_set1_epi64x(0xFFFFFFFF00000000);

    cpx256_t 
        a0, a1,
        b0, b1, b2, b3, b4, 
        real_res, imag_res;

    cpx256_t x1_pack;
    cpx256_t x0_pack;
    cpx256_t y_pack;

    float dt = dt0;
    for (int i = 0; i < M; i++) {
        // Vectorised cos(dt) + jsin(dt)
        dt_pack = _mm256_set1_ps(dt);
        dt_pack = _mm256_add_ps(dt_pack, dt_step_pack.ps);
        dt += dt_step_pack_stride;
        if (is_large_offset) {
            dt = std::fmod(dt, 2.0f*(float)M_PI);
        }
        x1_pack.ps = _mm256_cos_ps(dt_pack);

        // Perform vectorised complex multiplication
        memcpy(x0_pack.f32, &x0[i*K], sizeof(std::complex<float>)*K);

        // Step 1: Calculate real component
        // [ac bd]
        a0.ps = _mm256_mul_ps(x0_pack.ps, x1_pack.ps);
        // [bd ..]
        a1.i = _mm256_bsrli_epi128(a0.i, 4);
        // [ac-bd 0]
        real_res.ps = _mm256_sub_ps(a0.ps, a1.ps);
        real_res.i = _mm256_and_si256(real_res.i, real_mask.i);

        // Step 2: Calculate imaginary component
        // Step 2.1: Swap c and d components
        // [d 0]
        b0.i = _mm256_bsrli_epi128(x1_pack.i, 4);
        b0.i = _mm256_and_si256(b0.i, real_mask.i);
        // [0 c]
        b1.i = _mm256_bslli_epi128(x1_pack.i, 4);
        b1.i = _mm256_and_si256(b1.i, imag_mask.i);
        // [d c]
        b2.i = _mm256_or_si256(b0.i, b1.i);

        // Step 2.2: Compute imaginary component
        // [ad bc]
        b3.ps = _mm256_mul_ps(x0_pack.ps, b2.ps);
        // [.. ad]
        b4.i = _mm256_bslli_epi128(b3.i, 4);
        // [0 bc+ad]
        imag_res.ps = _mm256_add_ps(b3.ps, b4.ps);
        imag_res.i = _mm256_and_si256(imag_res.i, imag_mask.i);

        // Step 3: Combine the real and imaginary components together
        y_pack.i = _mm256_or_si256(real_res.i, imag_res.i);
        memcpy(&y[i*K], y_pack.f32, sizeof(std::complex<float>)*K);
    }

    const size_t N_vector = M*K;
    dt = apply_pll_scalar(x0.subspan(N_vector), y.subspan(N_vector), freq_offset, dt);

    return dt0;
}
#endif

// Manual SSSE3 code which is up to 2x 
#if defined(__SSSE3__)
float apply_pll_ssse3(
    tcb::span<const std::complex<float>> x0, 
    tcb::span<std::complex<float>> y, 
    const float freq_offset,
    const float dt0) 
{
    const auto N = x0.size();
    const float dt_step = 2.0f * (float)M_PI * freq_offset * Ts;
    const bool is_large_offset = std::abs(freq_offset) > 1500.0f;

    // 128bits = 16bytes = 2*8bytes
    const int K = 2;
    const auto M = N/K;

    // Vectorise calculation of cos(dt) + jsin(dt)
    __m128 dt_pack;
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
    // We need to do component wise multiplication
    // This is done using:
    // 1. Shifting of the 256bit array
    // 2. Masking of the 256bit array
    cpx128_t real_mask, imag_mask;
    real_mask.i = _mm_set1_epi64x(0x00000000FFFFFFFF);
    imag_mask.i = _mm_set1_epi64x(0xFFFFFFFF00000000);

    cpx128_t 
        a0, a1,
        b0, b1, b2, b3, b4, 
        real_res, imag_res;

    cpx128_t x1_pack;
    cpx128_t x0_pack;
    cpx128_t y_pack;

    float dt = dt0;
    for (int i = 0; i < M; i++) {
        // Vectorised cos(dt) + jsin(dt)
        dt_pack = _mm_set1_ps(dt);
        dt_pack = _mm_add_ps(dt_pack, dt_step_pack.ps);
        dt += dt_step_pack_stride;
        if (is_large_offset) {
            dt = std::fmod(dt, 2.0f*(float)M_PI);
        }
        x1_pack.ps = _mm_cos_ps(dt_pack);

        // Perform vectorised complex multiplication
        memcpy(x0_pack.f32, &x0[i*K], sizeof(std::complex<float>)*K);

        // Step 1: Calculate real component
        // [ac bd]
        a0.ps = _mm_mul_ps(x0_pack.ps, x1_pack.ps);
        // [bd ..]
        a1.i = _mm_bsrli_si128(a0.i, 4);
        // [ac-bd 0]
        real_res.ps = _mm_sub_ps(a0.ps, a1.ps);
        real_res.i = _mm_and_si128(real_res.i, real_mask.i);

        // Step 2: Calculate imaginary component
        // Step 2.1: Swap c and d components
        // [d 0]
        b0.i = _mm_bsrli_si128(x1_pack.i, 4);
        b0.i = _mm_and_si128(b0.i, real_mask.i);
        // [0 c]
        b1.i = _mm_bslli_si128(x1_pack.i, 4);
        b1.i = _mm_and_si128(b1.i, imag_mask.i);
        // [d c]
        b2.i = _mm_or_si128(b0.i, b1.i);

        // Step 2.2: Compute imaginary component
        // [ad bc]
        b3.ps = _mm_mul_ps(x0_pack.ps, b2.ps);
        // [.. ad]
        b4.i = _mm_bslli_si128(b3.i, 4);
        // [0 bc+ad]
        imag_res.ps = _mm_add_ps(b3.ps, b4.ps);
        imag_res.i = _mm_and_si128(imag_res.i, imag_mask.i);

        // Step 3: Combine the real and imaginary components together
        y_pack.i = _mm_or_si128(real_res.i, imag_res.i);
        memcpy(&y[i*K], y_pack.f32, sizeof(std::complex<float>)*K);
    }

    const size_t N_vector = M*K;
    dt = apply_pll_scalar(x0.subspan(N_vector), y.subspan(N_vector), freq_offset, dt);

    return dt0;
}
#endif
