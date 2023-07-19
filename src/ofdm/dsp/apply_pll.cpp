#define _USE_MATH_DEFINES
#include <math.h>
#include <assert.h>
#include <stdalign.h>

#include "detect_architecture.h"
#include "simd_flags.h"
#include "./apply_pll.h"
#include "../ofdm_constants.h"

float apply_pll_scalar(
    tcb::span<const std::complex<float>> x0, 
    tcb::span<std::complex<float>> y, 
    const float freq_offset, const float dt0)
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

// x86
#if defined(__ARCH_X86__)
#include <immintrin.h>
#include "./x86/c32_mul.h"

void create_dt_step_simd(float* arr, const size_t K, const float dt) {
    float x = 0.0f;
    for (size_t i = 0; i < K; i++) {
        // cos(dt)
        arr[2*i+0] = x;
        // cos(dt-pi/2) = sin(dt)
        arr[2*i+1] = x - ((float)M_PI / 2.0f);
        x += dt;
    }
}

#if defined(__SSE3__)

#if !defined(_MSC_VER)
#define USE_SSE_AUTO
#define SSE_MATHFUN_WITH_CODE
#include "./x86/sse_mathfun.h"
#define _mm_cos_ps(x) cos_ps(x)
#endif

float apply_pll_sse3(
    tcb::span<const std::complex<float>> x0, 
    tcb::span<std::complex<float>> y, 
    const float freq_offset, const float dt0) 
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
    alignas(16) float dt_step_pack_arr[K*2u];
    create_dt_step_simd(dt_step_pack_arr, K, dt_step);
    const __m128 dt_step_pack = _mm_load_ps(dt_step_pack_arr);

    float dt = dt0;
    for (size_t i = 0; i < N_vector; i+=K) {
        // Update dt
        __m128 dt_pack = _mm_set1_ps(dt);
        dt_pack = _mm_add_ps(dt_pack, dt_step_pack);
        dt += dt_step_pack_stride;
        if (is_large_offset) {
            dt = std::fmod(dt, 2.0f*(float)M_PI);
        }

        __m128 pll = _mm_cos_ps(dt_pack);
        __m128 X = _mm_loadu_ps(reinterpret_cast<const float*>(&x0[i]));
        __m128 Y = c32_mul_sse3(X, pll);
        _mm_storeu_ps(reinterpret_cast<float*>(&y[i]), Y);
    }

    dt = apply_pll_scalar(x0.subspan(N_vector), y.subspan(N_vector), freq_offset, dt);
    return dt;
}
#endif

#if defined(__AVX__)

#if !defined(_MSC_VER)
#include "./x86/avx_mathfun.h"
#define _mm256_cos_ps(x) cos256_ps(x)
#endif

float apply_pll_avx(
    tcb::span<const std::complex<float>> x0, 
    tcb::span<std::complex<float>> y, 
    const float freq_offset, const float dt0) 
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
    alignas(32) float dt_step_pack_arr[K*2];
    create_dt_step_simd(dt_step_pack_arr, K, dt_step);
    const __m256 dt_step_pack = _mm256_loadu_ps(dt_step_pack_arr);

    float dt = dt0;
    for (size_t i = 0; i < N_vector; i+=K) {
        // Update dt
        __m256 dt_pack = _mm256_set1_ps(dt);
        dt_pack = _mm256_add_ps(dt_pack, dt_step_pack);
        dt += dt_step_pack_stride;
        if (is_large_offset) {
            dt = std::fmod(dt, 2.0f*(float)M_PI);
        }

        __m256 pll = _mm256_cos_ps(dt_pack);
        __m256 X = _mm256_loadu_ps(reinterpret_cast<const float*>(&x0[i]));
        __m256 Y = c32_mul_avx(X, pll);
        _mm256_storeu_ps(reinterpret_cast<float*>(&y[i]), Y);
    }

    dt = apply_pll_scalar(x0.subspan(N_vector), y.subspan(N_vector), freq_offset, dt);
    return dt;
}
#endif

#endif

float apply_pll_auto(
    tcb::span<const std::complex<float>> x0, 
    tcb::span<std::complex<float>> y, 
    const float freq_offset, const float dt0
) {
    #if defined(__ARCH_X86__)
        #if defined(__AVX__)
        return apply_pll_avx(x0, y, freq_offset, dt0);
        #elif defined(__SSE3__)
        return apply_pll_sse3(x0, y, freq_offset, dt0);
        #else
        return apply_pll_scalar(x0, y, freq_offset, dt0);
        #endif
    #else
        return apply_pll_scalar(x0, y, freq_offset, dt0);
    #endif
}

