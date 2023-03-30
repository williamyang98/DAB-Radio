#pragma once
#define _USE_MATH_DEFINES
#include <math.h>
#include <assert.h>
#include <complex>
#include "utility/span.h"
#include "../../ofdm_constants.h"

inline static
float apply_pll_scalar(
    tcb::span<const std::complex<float>> x0, 
    tcb::span<std::complex<float>> y, 
    const float freq_offset, const float dt0)
{
    assert(x0.size() == y.size());
    const size_t N = x0.size();
    const float dt_step = 2.0f * (float)M_PI * freq_offset * Ts;
    const bool is_large_offset = std::abs(freq_offset) > Fmax_wrap;

    float dt = dt0;
    for (size_t i = 0; i < N; i++) {
        const auto pll = std::complex<float>(
            std::cos(dt),
            std::sin(dt)
        );

        y[i] = x0[i] * pll;
        dt += dt_step;
        if (is_large_offset) {
            dt = std::fmod(dt, 2.0f*(float)M_PI);
        }
    }
    return dt;
}