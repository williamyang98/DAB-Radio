#include "../scalar/complex_conj_mul_sum.h"

#include "./complex_conj_mul_sum.h"
#include "./c32_conj_mul.h"
#include "./data_packing.h"

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

    auto y = std::complex<float>(0,0);
    for (size_t i = 0; i < N_vector; i+=K) {
        __m256 X0 = _mm256_loadu_ps(reinterpret_cast<const float*>(&x0[i]));
        __m256 X1 = _mm256_loadu_ps(reinterpret_cast<const float*>(&x1[i]));
        cpx256_t c1;
        c1.ps = c32_conj_mul_avx2(X0, X1);

        // Perform vectorised cumulative sum
        // Shift half of vector and add. Repeat until we get the final sum
        // [v1+v3 v2+v4]
        cpx128_t c2;
        cpx128_t c3;
        c2.ps = _mm_add_ps(c1.m128[0], c1.m128[1]);
        // [v2+v4 0]
        c3.i = _mm_srli_si128(c2.i, sizeof(std::complex<float>));
        // [v1+v2+v3+v4 0]
        c2.ps = _mm_add_ps(c2.ps, c3.ps);

        y += c2.c32[0];
    }

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

    auto y = std::complex<float>(0,0);
    for (size_t i = 0; i < N_vector; i+=K) {
        __m128 X0 = _mm_loadu_ps(reinterpret_cast<const float*>(&x0[i]));
        __m128 X1 = _mm_loadu_ps(reinterpret_cast<const float*>(&x1[i]));
        cpx128_t c1;
        c1.ps = c32_conj_mul_ssse3(X0, X1);

        // Perform vectorised cumulative sum
        // Shift half of vector and add. Repeat until we get the final sum
        cpx128_t c2;
        // [v2 0]
        c2.i = _mm_srli_si128(c1.i, sizeof(std::complex<float>));
        // [v1+v2 0]
        c1.ps = _mm_add_ps(c1.ps, c2.ps);

        y += c1.c32[0];
    }

    y += complex_conj_mul_sum_scalar(x0.subspan(N_vector), x1.subspan(N_vector));
    return y;
}
#endif