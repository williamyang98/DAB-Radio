#include "./complex_conj_mul_sum.h"
#include "./scalar/complex_conj_mul_sum.h"
#include <assert.h>

#include "./dsp_config.h"
#include "detect_architecture.h"

#if defined(__ARCH_X86__)
#include "./x86/complex_conj_mul_sum.h"
#endif

std::complex<float> complex_conj_mul_sum_auto(
    tcb::span<const std::complex<float>> x0,
    tcb::span<const std::complex<float>> x1)
{
    #if defined(_OFDM_DSP_AVX2)
    return complex_conj_mul_sum_avx2(x0, x1);
    #elif defined(_OFDM_DSP_SSSE3)
    return complex_conj_mul_sum_ssse3(x0, x1);
    #else
    return complex_conj_mul_sum_scalar(x0, x1);
    #endif
}