#define _USE_MATH_DEFINES
#include <math.h>
#include "apply_pll.h"
#include "data_packing.h"
#include "c32_mul.h"
// TODO: Modify code to support ARM platforms like Raspberry PI using NEON
#include <immintrin.h>
#include <complex>
#include <assert.h>

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

// NOTE: Fixed rate sample rate for OFDM
constexpr float Ts = 1.0f/2.048e6f;
constexpr float Fmax_wrap = 1.5e3f;

float apply_pll_scalar(
    tcb::span<const std::complex<float>> x0, 
    tcb::span<std::complex<float>> y, 
    const float freq_offset,
    const float dt0)
{
    assert(x0.size() == y.size());
    const size_t N = x0.size();
    const float dt_step = 2.0f * (float)M_PI * freq_offset * Ts;
    const bool is_large_offset = std::abs(freq_offset) > Fmax_wrap;

    float dt = dt0;
    for (size_t i = 0; i < N; i++) {
        const auto pll = std::complex<float>(
            std::cos(dt),
            std::sin(dt)
        );

        y[i] = x0[i] * pll;
        dt += dt_step;
        if (is_large_offset) {
            dt = std::fmod(dt, 2.0f*(float)M_PI);
        }
    }
    return dt;
}

// Manual AVX2 code which is up to 4x faster
#if defined(_OFDM_DSP_AVX2)
float apply_pll_avx2(
    tcb::span<const std::complex<float>> x0, 
    tcb::span<std::complex<float>> y, 
    const float freq_offset,
    const float dt0) 
{
    assert(x0.size() == y.size());
    const size_t N = x0.size();
    const float dt_step = 2.0f * (float)M_PI * freq_offset * Ts;
    const bool is_large_offset = std::abs(freq_offset) > Fmax_wrap;

    // 256bits = 32bytes = 4*8bytes
    const size_t K = 4u;
    const size_t M = N/K;
    const size_t N_vector = M*K;

    // cos(dt) + jsin(dt) = cos(dt) + jcos(dt-PI/2)
    const float dt_step_pack_stride = dt_step * K;
    cpx256_t dt_step_pack;
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

    float dt = dt0;
    for (size_t i = 0; i < N_vector; i+=K) {
        // Update dt
        __m256 dt_pack = _mm256_set1_ps(dt);
        dt_pack = _mm256_add_ps(dt_pack, dt_step_pack.ps);
        dt += dt_step_pack_stride;
        if (is_large_offset) {
            dt = std::fmod(dt, 2.0f*(float)M_PI);
        }

        __m256 pll = _mm256_cos_ps(dt_pack);
        __m256 X = _mm256_loadu_ps(reinterpret_cast<const float*>(&x0[i]));
        __m256 Y = c32_mul_avx2(X, pll);
        _mm256_storeu_ps(reinterpret_cast<float*>(&y[i]), Y);
    }

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
    const size_t N = x0.size();
    const float dt_step = 2.0f * (float)M_PI * freq_offset * Ts;
    const bool is_large_offset = std::abs(freq_offset) > Fmax_wrap;

    // 128bits = 16bytes = 2*8bytes
    const size_t K = 2u;
    const size_t M = N/K;
    const size_t N_vector = M*K;

    // Generate dt vector
    // cos(dt) + jsin(dt) = cos(dt) + jcos(dt-PI/2)
    const float dt_step_pack_stride = dt_step * K;
    cpx128_t dt_step_pack;
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

    float dt = dt0;
    for (size_t i = 0; i < N_vector; i+=K) {
        // Update dt
        __m128 dt_pack = _mm_set1_ps(dt);
        dt_pack = _mm_add_ps(dt_pack, dt_step_pack.ps);
        dt += dt_step_pack_stride;
        if (is_large_offset) {
            dt = std::fmod(dt, 2.0f*(float)M_PI);
        }

        __m128 pll = _mm_cos_ps(dt_pack);
        __m128 X = _mm_loadu_ps(reinterpret_cast<const float*>(&x0[i]));
        __m128 Y = c32_mul_ssse3(X, pll);
        _mm_storeu_ps(reinterpret_cast<float*>(&y[i]), Y);
    }

    dt = apply_pll_scalar(x0.subspan(N_vector), y.subspan(N_vector), freq_offset, dt);
    return dt;
}
#endif
