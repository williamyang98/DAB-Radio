#include <assert.h>
#include <stdalign.h> // NOLINT
#include <stddef.h>
#include <cmath>
#include <complex>
#include "detect_architecture.h"
#include "simd_flags.h" // NOLINT
#include "utility/span.h"
#include "./apply_pll.h"
#include "./chebyshev_sine.h"

static void apply_pll_scalar(
    tcb::span<const std::complex<float>> x, tcb::span<std::complex<float>> y, 
    const float freq_norm, const float dt_norm)
{
    assert(x.size() == y.size());
    const size_t N = x.size();
    const float dt_step = freq_norm;
    for (size_t i = 0; i < N; i++) {
        float dt_sin = dt_norm + float(i)*dt_step;
        float dt_cos = dt_sin+0.25f;
        // translate to [-0.5,+0.5] within chebyshev accurate range
        dt_sin = dt_sin - std::round(dt_sin);
        dt_cos = dt_cos - std::round(dt_cos);
        const float cos = chebyshev_sine(dt_cos);
        const float sin = chebyshev_sine(dt_sin);
        const auto pll = std::complex<float>(cos, sin);
        y[i] = x[i] * pll;
    }
}

// x86
#if defined(__ARCH_X86__)

#if defined(__SSE3__)
#include <smmintrin.h>
#include <xmmintrin.h>
#include "./x86/c32_mul.h"

static void apply_pll_sse3(
    tcb::span<const std::complex<float>> x, tcb::span<std::complex<float>> y, 
    const float freq_norm, const float dt_norm) 
{
    assert(x.size() == y.size());
    const size_t N = x.size();

    // 128bits = 16bytes = 2*8bytes
    const size_t K = 2u;
    const size_t M = N/K;
    const size_t N_vector = M*K;

    const float dt_step = freq_norm;
    alignas(16) float dt_step_pack_arr[K*2u];
    for (size_t i = 0; i < K; i++) {
        const float dt = float(i)*dt_step;
        dt_step_pack_arr[2*i+0] = dt+0.25f; // f(x) = cos(2*PI*x) = sin[2*PI*(x+0.25)]
        dt_step_pack_arr[2*i+1] = dt;
    }
    const __m128 dt_step_pack = _mm_load_ps(dt_step_pack_arr);
    for (size_t i = 0; i < N_vector; i+=K) {
        __m128 dt = _mm_set1_ps(dt_norm + float(i)*dt_step);
        dt = _mm_add_ps(dt, dt_step_pack);
        // translate to [-0.5,+0.5] within chebyshev accurate range
        constexpr int ROUND_FLAGS = _MM_FROUND_TO_NEAREST_INT | _MM_FROUND_NO_EXC;
        dt = _mm_sub_ps(dt, _mm_round_ps(dt, ROUND_FLAGS)); 
        __m128 pll = _mm_chebyshev_sine(dt);
        __m128 X = _mm_loadu_ps(reinterpret_cast<const float*>(&x[i]));
        __m128 Y = c32_mul_sse3(X, pll);
        _mm_storeu_ps(reinterpret_cast<float*>(&y[i]), Y);
    }
 
    const float dt_scalar = dt_norm + float(N_vector)*dt_step;
    apply_pll_scalar(x.subspan(N_vector), y.subspan(N_vector), freq_norm, dt_scalar);
}
#endif

#if defined(__AVX__)
#include <immintrin.h>
#include <smmintrin.h>
#include "./x86/c32_mul.h"

static void apply_pll_avx(
    tcb::span<const std::complex<float>> x, tcb::span<std::complex<float>> y, 
    const float freq_norm, const float dt_norm) 
{
    assert(x.size() == y.size());
    const size_t N = x.size();

    // 256bits = 32bytes = 4*8bytes
    const size_t K = 4u;
    const size_t M = N/K;
    const size_t N_vector = M*K;
 
    const float dt_step = freq_norm;
    alignas(32) float dt_step_pack_arr[K*2u];
    for (size_t i = 0; i < K; i++) {
        const float dt = float(i)*dt_step;
        dt_step_pack_arr[2*i+0] = dt+0.25f; // f(x) = cos(2*PI*x) = sin[2*PI*(x+0.25)]
        dt_step_pack_arr[2*i+1] = dt;
    }
    const __m256 dt_step_pack = _mm256_loadu_ps(dt_step_pack_arr);
    for (size_t i = 0; i < N_vector; i+=K) {
        __m256 dt = _mm256_set1_ps(dt_norm + float(i)*dt_step);
        dt = _mm256_add_ps(dt, dt_step_pack);
        // translate to [-0.5,+0.5] within chebyshev accurate range
        constexpr int ROUND_FLAGS = _MM_FROUND_TO_NEAREST_INT | _MM_FROUND_NO_EXC;
        dt = _mm256_sub_ps(dt, _mm256_round_ps(dt, ROUND_FLAGS)); 
        __m256 pll = _mm256_chebyshev_sine(dt);
        __m256 X = _mm256_loadu_ps(reinterpret_cast<const float*>(&x[i]));
        __m256 Y = c32_mul_avx(X, pll);
        _mm256_storeu_ps(reinterpret_cast<float*>(&y[i]), Y);
    }
 
    const float dt_scalar = dt_norm + float(N_vector)*dt_step;
    apply_pll_scalar(x.subspan(N_vector), y.subspan(N_vector), freq_norm, dt_scalar);
}
#endif

#endif

void apply_pll_auto(
    tcb::span<const std::complex<float>> x, tcb::span<std::complex<float>> y, 
    const float freq_norm, const float dt_norm
) {
    #if defined(__ARCH_X86__)
        #if defined(__AVX__)
        apply_pll_avx(x, y, freq_norm, dt_norm);
        #elif defined(__SSE3__)
        apply_pll_sse3(x, y, freq_norm, dt_norm);
        #else
        apply_pll_scalar(x, y, freq_norm, dt_norm);
        #endif
    #else
        apply_pll_scalar(x, y, freq_norm, dt_norm);
    #endif
}

