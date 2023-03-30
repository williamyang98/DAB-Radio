#pragma once
#include <cmath>
#include <complex>
#include <assert.h>
#include "utility/span.h"

inline static
std::complex<float> complex_conj_mul_sum_scalar(
    tcb::span<const std::complex<float>> x0,
    tcb::span<const std::complex<float>> x1)
{
    // DOC: docs/DAB_implementation_in_SDR_detailed.pdf
    // Clause 3.13.1 - Fraction frequency offset estimation
    assert(x0.size() == x1.size());
    const size_t N = x0.size();
    auto y = std::complex<float>(0,0);
    for (size_t i = 0; i < N; i++) {
        y += x0[i] * std::conj(x1[i]);
    }
    return y;
}