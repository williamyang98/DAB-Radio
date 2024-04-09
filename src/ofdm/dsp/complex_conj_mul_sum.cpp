#include <assert.h>
#include <stddef.h>
#include <complex>
#include "detect_architecture.h"
#include "simd_flags.h" // NOLINT
#include "utility/span.h"
#include "./complex_conj_mul_sum.h"

std::complex<float> complex_conj_mul_sum_scalar(
    tcb::span<const std::complex<float>> x0,
    tcb::span<const std::complex<float>> x1)
{
    // DOC: docs/DAB_implementation_in_SDR_detailed.pdf
    // Clause 3.13.1 - Fraction frequency offset estimation
    assert(x0.size() == x1.size());
    const size_t N = x0.size();
    auto y = std::complex<float>(0,0);
    for (size_t i = 0; i < N; i++) {
        y += x0[i] * std::conj(x1[i]);
    }
    return y;
}

#if defined(__ARCH_X86__)
#include "./x86/c32_conj_mul.h"

#if defined(__SSE3__)
#include <xmmintrin.h>
std::complex<float> complex_conj_mul_sum_sse3(
    tcb::span<const std::complex<float>> x0,
    tcb::span<const std::complex<float>> x1)
{
    assert(x0.size() == x1.size());
    const size_t N = x0.size();

    // 128bits = 16bytes = 2*8bytes
    const size_t K = 2u;
    const size_t M = N/K;
    const size_t N_vector = M*K;

    __m128 Y_vec = _mm_set1_ps(0.0f);
    for (size_t i = 0; i < N_vector; i+=K) {
        __m128 X0 = _mm_loadu_ps(reinterpret_cast<const float*>(&x0[i]));
        __m128 X1 = _mm_loadu_ps(reinterpret_cast<const float*>(&x1[i]));
        __m128 Y = c32_conj_mul_sse3(X0, X1);
        Y_vec = _mm_add_ps(Y, Y_vec);
    }

    // [c1 c2]
    // [c1+c2 0]
    Y_vec = _mm_add_ps(Y_vec, _mm_shuffle_ps(Y_vec, Y_vec, 0b0000'1110));
    // Extract real and imaginary components
    auto y = std::complex<float>{
        _mm_cvtss_f32(Y_vec),
        _mm_cvtss_f32(_mm_shuffle_ps(Y_vec, Y_vec, 0b000000'01)),
    };

    y += complex_conj_mul_sum_scalar(x0.subspan(N_vector), x1.subspan(N_vector));
    return y;
}
#endif

#if defined(__AVX__)
#include <immintrin.h>
std::complex<float> complex_conj_mul_sum_avx(
    tcb::span<const std::complex<float>> x0,
    tcb::span<const std::complex<float>> x1)
{
    assert(x0.size() == x1.size());
    const size_t N = x0.size();

    // 256bits = 32bytes = 4*8bytes
    const size_t K = 4u;
    const size_t M = N/K;
    const size_t N_vector = M*K;

    __m256 Y_vec = _mm256_set1_ps(0.0f);
    for (size_t i = 0; i < N_vector; i+=K) {
        __m256 X0 = _mm256_loadu_ps(reinterpret_cast<const float*>(&x0[i]));
        __m256 X1 = _mm256_loadu_ps(reinterpret_cast<const float*>(&x1[i]));
        __m256 Y = c32_conj_mul_avx(X0, X1);
        Y_vec = _mm256_add_ps(Y, Y_vec);
    }

    // Perform vectorised cumulative sum
    // [c1 c2 c3 c4]
    // [c1+c3 c2+c4]
    __m128 v0 = _mm_add_ps(_mm256_extractf128_ps(Y_vec, 0), _mm256_extractf128_ps(Y_vec, 1));
    // [c1+c2+c3+c4 0]
    v0 = _mm_add_ps(v0, _mm_permute_ps(v0, 0b0000'1110));
    // Extract real and imaginary components
    auto y = std::complex<float>{
        _mm_cvtss_f32(v0),
        _mm_cvtss_f32(_mm_permute_ps(v0, 0b000000'01)),
    };

    y += complex_conj_mul_sum_scalar(x0.subspan(N_vector), x1.subspan(N_vector));
    return y;
}
#endif

#endif

std::complex<float> complex_conj_mul_sum_auto(
    tcb::span<const std::complex<float>> x0,
    tcb::span<const std::complex<float>> x1)
{
    #if defined(__ARCH_X86__)
        #if defined(__AVX__)
        return complex_conj_mul_sum_avx(x0, x1);
        #elif defined(__SSE3__)
        return complex_conj_mul_sum_sse3(x0, x1);
        #else
        return complex_conj_mul_sum_scalar(x0, x1);
        #endif
    #else
        return complex_conj_mul_sum_scalar(x0, x1);
    #endif
}