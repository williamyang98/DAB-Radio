#pragma once

#include <complex>
#include "utility/span.h"

std::complex<float> complex_conj_mul_sum_auto(
    tcb::span<const std::complex<float>> x0,
    tcb::span<const std::complex<float>> x1
);
