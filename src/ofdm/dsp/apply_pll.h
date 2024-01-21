#pragma once

#include <complex>
#include "utility/span.h"

// freq_norm = frequency/sampling_rate
void apply_pll_auto(
    tcb::span<const std::complex<float>> x, tcb::span<std::complex<float>> y,
    const float freq_norm, const float dt_norm=0.0f
);
