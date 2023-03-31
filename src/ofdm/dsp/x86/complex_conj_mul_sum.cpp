#include "../scalar/complex_conj_mul_sum.h"

#include "./complex_conj_mul_sum.h"
#include "./c32_conj_mul.h"

#include <complex>
#include <assert.h>
#include <immintrin.h>

#if defined(_OFDM_DSP_AVX2)
std::complex<float> complex_conj_mul_sum_avx2(
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
        __m256 Y = c32_conj_mul_avx2(X0, X1);
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

#if defined(_OFDM_DSP_SSSE3)
std::complex<float> complex_conj_mul_sum_ssse3(
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
        __m128 Y = c32_conj_mul_ssse3(X0, X1);
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