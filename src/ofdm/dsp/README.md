# Introduction
This is a collection of DSP functions used in OFDM demodulation.

| Function | Description |
| --- | --- |
| apply_pll | y(t) = x(t) * [cos(2πft) + j*sin(2πft)] |
| complex_conj_mul_sum | y = Σ x0(t) * conj[x1(t)]  |

# Vectorisation
The DSP functions have a scalar and vectorised variants. 
The scalar variants are portable to any platform whereas the vectorised variants only work on supported targets.

| Target | Lane Width | Speedup |
| --- | --- | --- |
| x86 AVX2  | 256 bits | x4 |
| x86 SSSE3 | 128 bits | x2 |
| AARCH64   | 128 bits | x2 |

