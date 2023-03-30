#include "./scalar/apply_pll.h"
#include "./apply_pll.h"

#include "./dsp_config.h"
#include "detect_architecture.h"

#if defined(__ARCH_X86__)
#include "./x86/apply_pll.h"
#endif

float apply_pll_auto(
    tcb::span<const std::complex<float>> x0, 
    tcb::span<std::complex<float>> y, 
    const float freq_offset, const float dt0
) {
    #if defined(_OFDM_DSP_AVX2)
    return apply_pll_avx2(x0, y, freq_offset, dt0);
    #elif defined(_OFDM_DSP_SSSE3)
    return apply_pll_ssse3(x0, y, freq_offset, dt0);
    #else
    return apply_pll_scalar(x0, y, freq_offset, dt0);
    #endif
}
