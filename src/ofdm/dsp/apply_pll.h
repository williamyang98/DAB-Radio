#pragma once

#include <complex>
#include "utility/span.h"

float apply_pll_auto(
    tcb::span<const std::complex<float>> x0, 
    tcb::span<std::complex<float>> y, 
    const float freq_offset, const float dt0=0.0f
);
