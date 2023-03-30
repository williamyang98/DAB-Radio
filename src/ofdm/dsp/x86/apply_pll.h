#pragma once

#include "../dsp_config.h"
#include "utility/span.h"
#include <complex>

#if defined(_OFDM_DSP_AVX2)
float apply_pll_avx2(
    tcb::span<const std::complex<float>> x0, 
    tcb::span<std::complex<float>> y, 
    const float freq_offset,
    const float dt0=0.0f);
#endif

#if defined(_OFDM_DSP_SSSE3)
float apply_pll_ssse3(
    tcb::span<const std::complex<float>> x0, 
    tcb::span<std::complex<float>> y, 
    const float freq_offset,
    const float dt0=0.0f);
#endif