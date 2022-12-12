#include "complex_conj_mul_sum.h"

#include <complex>
#include <assert.h>

std::complex<float> complex_conj_mul_sum_scalar(
    tcb::span<const std::complex<float>> x0,
    tcb::span<const std::complex<float>> x1)
{
    // DOC: docs/DAB_implementation_in_SDR_detailed.pdf
    // Clause 3.13.1 - Fraction frequency offset estimation
    assert(x0.size() == x1.size());
    const size_t N = x0.size();
    auto y = std::complex<float>(0,0);
    for (int i = 0; i < N; i++) {
        y += x0[i] * std::conj(x1[i]);
    }
    return y;
}

// TODO: Modify code to support ARM platforms like Raspberry PI using NEON
#include <immintrin.h>

#ifdef _MSC_VER
#define ALIGNED(x) __declspec(align(x))
#else
#define ALIGNED(x) __attribute__ ((aligned(x)))
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

#if defined(_OFDM_DSP_AVX2)
std::complex<float> complex_conj_mul_sum_avx2(
    tcb::span<const std::complex<float>> x0,
    tcb::span<const std::complex<float>> x1)
{
    assert(x0.size() == x1.size());
    const size_t N = x0.size();
    auto y = std::complex<float>(0,0);

    // 256bits = 32bytes = 4*8bytes
    const int K = 4;
    const auto M = N/K;

    // Vectorise complex multiplication
    // [3 2 1 0] -> [2 3 0 1]
    const uint8_t SWAP_COMPONENT_MASK = 0b10110001;
    // [3 2 1 0] -> [2 2 0 0]
    const uint8_t GET_REAL_MASK = 0b10100000;
    // [3 2 1 0] -> [3 3 1 1]
    const uint8_t GET_IMAG_MASK = 0b11110101;

    for (int i = 0; i < M; i++) {
        // Perform vectorised complex conjugate multiplication
        // y = (a+bi)*(c-di)
        // y = (bd+ac) + i*(bc-ad)

        // NOTE: Use unaligned load
        // [a b]
        __m256 X0 = _mm256_loadu_ps(reinterpret_cast<const float*>(&x0[i*K]));
        // [c d]
        __m256 X1 = _mm256_loadu_ps(reinterpret_cast<const float*>(&x1[i*K]));

        // [d c]
        __m256 a0 = _mm256_permute_ps(X1, SWAP_COMPONENT_MASK);
        // [a a]
        __m256 a1 = _mm256_permute_ps(X0, GET_REAL_MASK);
        // [b b]
        __m256 a2 = _mm256_permute_ps(X0, GET_IMAG_MASK);

        // [ad ac]
        __m256 b0 = _mm256_mul_ps(a1, a0);

        #if !defined(_OFDM_DSP_FMA)
        // [bc bd]
        __m256 b1 = _mm256_mul_ps(a2, X1);
        // [bc-ad bd+ac]
        __m256 c0 = _mm256_addsub_ps(b1, b0);
        #else
        // [bc-ad bd+ac]
        __m256 c0 = _mm256_fmaddsub_ps(a2, X1, b0);
        #endif

        // [bd+ac bc-ad]
        cpx256_t c1;
        c1.ps = _mm256_permute_ps(c0, SWAP_COMPONENT_MASK);

        // Perform vectorised cumulative sum
        // Shift half of vector and add. Repeat until we get the final sum
        cpx128_t c2;
        cpx128_t c3;
        // [v1+v3 v2+v4]
        c2.ps = _mm_add_ps(c1.m128[0], c1.m128[1]);
        // [v2+v4 0]
        c3.i = _mm_srli_si128(c2.i, sizeof(std::complex<float>));
        // [v1+v2+v3+v4 0]
        c2.ps = _mm_add_ps(c2.ps, c3.ps);

        y += c2.c32[0];
    }

    const size_t N_vector = M*K;
    y += complex_conj_mul_sum_scalar(x0.subspan(N_vector), x1.subspan(N_vector));

    return y;
}
#endif

#if defined(_OFDM_DSP_SSSE3)
std::complex<float> complex_conj_mul_sum_ssse3(
    tcb::span<const std::complex<float>> x0,
    tcb::span<const std::complex<float>> x1)
{
    assert(x0.size() == x1.size());
    const size_t N = x0.size();
    auto y = std::complex<float>(0,0);

    // 128bits = 16bytes = 2*8bytes
    const int K = 2;
    const auto M = N/K;

    // Vectorise complex multiplication
    // [3 2 1 0] -> [2 3 0 1]
    const uint8_t SWAP_COMPONENT_MASK = 0b10110001;
    // [3 2 1 0] -> [2 2 0 0]
    const uint8_t GET_REAL_MASK = 0b10100000;
    // [3 2 1 0] -> [3 3 1 1]
    const uint8_t GET_IMAG_MASK = 0b11110101;

    for (int i = 0; i < M; i++) {
        // Perform vectorised complex conjugate multiplication
        // y = (a+bi)*(c-di)
        // y = (bd+ac) + i*(bc-ad)

        // NOTE: Use unaligned load
        // [a b]
        __m128 X0 = _mm_loadu_ps(reinterpret_cast<const float*>(&x0[i*K]));
        // [c d]
        __m128 X1 = _mm_loadu_ps(reinterpret_cast<const float*>(&x1[i*K]));

        // [d c]
        __m128 a0 = _mm_shuffle_ps(X1, X1, SWAP_COMPONENT_MASK);
        // [a a]
        __m128 a1 = _mm_shuffle_ps(X0, X0, GET_REAL_MASK);
        // [b b]
        __m128 a2 = _mm_shuffle_ps(X0, X0, GET_IMAG_MASK);

        // [ad ac]
        __m128 b0 = _mm_mul_ps(a1, a0);

        #if !defined(_OFDM_DSP_FMA)
        // [bc bd]
        __m128 b1 = _mm_mul_ps(a2, X1);
        // [bc-ad bd+ac]
        __m128 c0 = _mm_addsub_ps(b1, b0);
        #else
        // [bc-ad bd+ac]
        __m128 c0 = _mm_fmaddsub_ps(a2, X1, b0);
        #endif

        // [bd+ac bc-ad]
        cpx128_t c1;
        c1.ps = _mm_permute_ps(c0, SWAP_COMPONENT_MASK);

        // Perform vectorised cumulative sum
        // Shift half of vector and add. Repeat until we get the final sum
        cpx128_t c2;
        // [v2 0]
        c2.i = _mm_srli_si128(c1.i, sizeof(std::complex<float>));
        // [v1+v2 0]
        c1.ps = _mm_add_ps(c1.ps, c2.ps);

        y += c1.c32[0];
    }

    const size_t N_vector = M*K;
    y += complex_conj_mul_sum_scalar(x0.subspan(N_vector), x1.subspan(N_vector));

    return y;
}
#endif