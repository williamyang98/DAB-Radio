#pragma once

#include <complex>
#include "utility/span.h"
#include "../dsp_config.h"

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