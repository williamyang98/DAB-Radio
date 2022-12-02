#pragma once

#include <complex>
#include "utility/span.h"

float apply_pll_scalar(
    tcb::span<const std::complex<float>> x0, 
    tcb::span<std::complex<float>> y, 
    const float freq_offset,
    const float dt0=0.0f);

#if defined(__AVX2__)
float apply_pll_avx2(
    tcb::span<const std::complex<float>> x0, 
    tcb::span<std::complex<float>> y, 
    const float freq_offset,
    const float dt0=0.0f);
#endif

#if defined(__SSSE3__)
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
    #if defined(__AVX2__)
    #pragma message("PLL is using AVX2 code")
    return apply_pll_avx2(x0, y, freq_offset, dt0);
    #elif defined(__SSSE3__)
    #pragma message("PLL is using SSSE3 code")
    return apply_pll_ssse3(x0, y, freq_offset, dt0);
    #else
    #pragma message("PLL is using scalar code")
    return apply_pll_scalar(x0, y, freq_offset, dt0);
    #endif
}
