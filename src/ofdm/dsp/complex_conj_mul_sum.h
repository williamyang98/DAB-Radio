#pragma once

#include <complex>
#include "utility/span.h"
#include "dsp_config.h"

std::complex<float> complex_conj_mul_sum_scalar(
    tcb::span<const std::complex<float>> x0,
    tcb::span<const std::complex<float>> x1);

#if defined(_OFDM_DSP_AVX2)
std::complex<float> complex_conj_mul_sum_avx2(
    tcb::span<const std::complex<float>> x0,
    tcb::span<const std::complex<float>> x1);
#endif

#if defined(_OFDM_DSP_SSSE3)
std::complex<float> complex_conj_mul_sum_ssse3(
    tcb::span<const std::complex<float>> x0,
    tcb::span<const std::complex<float>> x1);
#endif

static std::complex<float> complex_conj_mul_sum_auto(
    tcb::span<const std::complex<float>> x0,
    tcb::span<const std::complex<float>> x1)
{
    #if defined(_OFDM_DSP_AVX2)
    return complex_conj_mul_sum_avx2(x0, x1);
    #elif defined(_OFDM_DSP_SSSE3)
    return complex_conj_mul_sum_ssse3(x0, x1);
    #else
    return complex_conj_mul_sum_scalar(x0, x1);
    #endif
}