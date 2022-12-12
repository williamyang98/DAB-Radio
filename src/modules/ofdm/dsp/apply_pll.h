#pragma once

#include <complex>
#include "utility/span.h"
#include "dsp_config.h"

float apply_pll_scalar(
    tcb::span<const std::complex<float>> x0, 
    tcb::span<std::complex<float>> y, 
    const float freq_offset,
    const float dt0=0.0f);

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

static float apply_pll_auto(
    tcb::span<const std::complex<float>> x0, 
    tcb::span<std::complex<float>> y, 
    const float freq_offset,
    const float dt0=0.0f) 
{
    #if defined(_OFDM_DSP_AVX2)
    return apply_pll_avx2(x0, y, freq_offset, dt0);
    #elif defined(_OFDM_DSP_SSSE3)
    return apply_pll_ssse3(x0, y, freq_offset, dt0);
    #else
    return apply_pll_scalar(x0, y, freq_offset, dt0);
    #endif
}
