## Introduction
This is a collection of DSP functions used in OFDM demodulation.

The DSP functions have a scalar and vectorised variants. 

The scalar variants are portable to any platform whereas the vectorised variants only work on x64 targets.

Vectorised variants can perform up to 4x faster for AVX2 or 2x faster for SSSE3.

| Function | Description |
| --- | --- |
| apply_pll | y(t) = x(t) * [cos(2πft) + j*sin(2πft)] |
| complex_conj_mul_sum | y = Σ x0(t) * conj[x1(t)]  |
