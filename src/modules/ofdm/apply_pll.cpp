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
#pragma message("Compiling PLL with external Intel SVML library")

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
    cpx256_t a0, a1, b0, b1;
    cpx256_t x1_pack;
    cpx256_t x0_pack;
    cpx256_t y_pack;

    // [3 2 1 0] -> [2 3 0 1]
    const uint8_t SWAP_COMPONENT_MASK = 0b10110001;

    // c = _mm_blend(a, b, BLEND_MASK)
    // bit[0:7] = 0   , 1   , 0   , 1   , 0   , 1   , 0,    1
    // c  [0:7] = a[0], b[1], a[2], b[3], a[4], b[5], a[6], b[7]
    const uint8_t A_B_BLEND_MASK = 0b10101010;
    const uint8_t B_A_BLEND_MASK = 0b01010101;

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
        // NOTE: We can use load_ps and store_ps for AVX2
        //       This is because they don't throw an exception when there is a misalignment
        //       Instead there will just be a performance penalty due cache line misalignment
        //       Source: https://blog.ngzhian.com/sse-avx-memory-alignment.html
        //       The intel intrinsics guide mistakenly says that AVX2 misalignment will throw a general exception
        //       The disassembly shows it uses an unaligned movups instead of aligned movaps
        x0_pack.ps = _mm256_load_ps(reinterpret_cast<const float*>(&x0[i*K]));

        // [ac bd]
        a0.ps = _mm256_mul_ps(x0_pack.ps, x1_pack.ps);
        // [bd ac]
        a0.ps = _mm256_permute_ps(a0.ps, SWAP_COMPONENT_MASK);

        // [d c]
        a1.ps = _mm256_permute_ps(x1_pack.ps, SWAP_COMPONENT_MASK);
        // [ad bc]
        a1.ps = _mm256_mul_ps(x0_pack.ps, a1.ps);

        // [ad ac]
        b0.ps = _mm256_blend_ps(a0.ps, a1.ps, B_A_BLEND_MASK);
        // [ac ad]
        b0.ps = _mm256_permute_ps(b0.ps, SWAP_COMPONENT_MASK);

        // [bd bc]
        b1.ps = _mm256_blend_ps(a0.ps, a1.ps, A_B_BLEND_MASK);

        // [ac-bd ad+bc]
        y_pack.ps = _mm256_addsub_ps(b0.ps, b1.ps);

        _mm256_store_ps(reinterpret_cast<float*>(&y[i*K]), y_pack.ps);
    }

    const size_t N_vector = M*K;
    dt = apply_pll_scalar(x0.subspan(N_vector), y.subspan(N_vector), freq_offset, dt);

    return dt;
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
    cpx128_t a0, a1, b0, b1;
    cpx128_t x1_pack;
    cpx128_t x0_pack;
    cpx128_t y_pack;

    // [3 2 1 0] -> [2 3 0 1]
    const uint8_t SWAP_COMPONENT_MASK = 0b10110001;
    // NOTE: For SSE3 we use _mm_shuffle_ps(a, a, MASK) instead of _mm_permute_ps(a, MASK)
    //       This is because _mm_permute_ps is a AVX intrinsic

    // _mm_blend_ps is a SSE4.1 instruction and not accessible to SSSE3
    // We manually implement it by masking and ORing data
    cpx128_t real_mask, imag_mask;
    real_mask.i = _mm_set1_epi64x(0x00000000FFFFFFFF);
    imag_mask.i = _mm_set1_epi64x(0xFFFFFFFF00000000);

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
        // NOTE: SSE alignment requirements are strict unlike AVX
        //       If there is a misalignment an exception will be thrown
        //       Therefore use memcpy instead of load_ps or store_ps
        memcpy(&x0_pack.f32[0], &x0[i*K], K*sizeof(std::complex<float>));

        // [ac bd]
        a0.ps = _mm_mul_ps(x0_pack.ps, x1_pack.ps);
        // [bd ac]
        a0.ps = _mm_shuffle_ps(a0.ps, a0.ps, SWAP_COMPONENT_MASK);

        // [d c]
        a1.ps = _mm_shuffle_ps(x1_pack.ps, x1_pack.ps, SWAP_COMPONENT_MASK);
        // [ad bc]
        a1.ps = _mm_mul_ps(x0_pack.ps, a1.ps);

        // [ad ac]
        b0.ps = _mm_or_ps(_mm_and_ps(a0.ps, imag_mask.ps), _mm_and_ps(a1.ps, real_mask.ps));
        // [ac ad]
        b0.ps = _mm_shuffle_ps(b0.ps, b0.ps, SWAP_COMPONENT_MASK);
        // [bd bc]
        b1.ps = _mm_or_ps(_mm_and_ps(a0.ps, real_mask.ps), _mm_and_ps(a1.ps, imag_mask.ps));

        // [ac-bd ad+bc]
        y_pack.ps = _mm_addsub_ps(b0.ps, b1.ps);

        memcpy(&y[i*K], &y_pack.f32[0], K*sizeof(std::complex<float>));
    }

    const size_t N_vector = M*K;
    dt = apply_pll_scalar(x0.subspan(N_vector), y.subspan(N_vector), freq_offset, dt);

    return dt;
}
#endif
