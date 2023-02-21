/* Generic Viterbi decoder,
 * Copyright Phil Karn, KA9Q,
 * Karn's original code can be found here: https://github.com/ka9q/libfec 
 * May be used under the terms of the GNU Lesser General Public License (LGPL)
 * see http://www.gnu.org/copyleft/lgpl.html
 */
#pragma once
#include "viterbi_decoder_core.h"
#include <stdint.h>
#include <stddef.h>
#include <assert.h>
#include <vector>
#include <immintrin.h>

// Vectorisation using SSE
//     16bit integers for errors, soft-decision values
//     8 way vectorisation from 128bits/16bits 
//     16bit decision type since 8 x 2 decisions bits per branch
// TODO: We can use int16_t for the error_t for a 33% performance improvement
//       With uint16_t we use adds_epu16 which uses saturated arithmetic and takes 0.5 CPI
//       With  int16_t we use add_epi16  which uses modular arithmetic and takes 0.33CPI
//       Refer to the Intel intrinsics guide for these numbers
// 
//       The original code uses int16_t and calculates: 
//           thresh = INT16_MAX-12750
//           bias   = min-INT16_MIN
//       This can cause the bias to overflow the INT16_MAX value and corrupt the error metrics 
//       By changing to the following:
//           bias   = min
//       Instead of renormalising to INT16_MIN, we renormalise to int16_t(0)
//       This is not ideal since we effectively halve the range of error values when renormalising
//       but allows us to use faster modular arithmetic when calculating the error metrics
//       This also applies to 8bit vectorisations and AVX
template <typename absolute_error_t = uint64_t>
class ViterbiDecoder_SSE_u16: public ViterbiDecoder_Core<uint16_t, int16_t, uint16_t, absolute_error_t>
{
private:
    using Base = ViterbiDecoder_Core<uint16_t, int16_t, uint16_t, absolute_error_t>;
public:
    static constexpr size_t ALIGN_AMOUNT = sizeof(__m128i);
    static constexpr size_t K_min = 5;
private:
    const size_t m128_width_metric;
    const size_t m128_width_branch_table;
    const size_t u16_width_decision; 
    std::vector<__m128i> m128_symbols;
public:
    // NOTE: branch_table.K >= 5 and branch_table.alignment >= 16  
    template <typename ... U>
    ViterbiDecoder_SSE_u16(U&& ... args)
    :   Base(std::forward<U>(args)...),
        // metric:       NUMSTATES   * sizeof(u16)                      = NUMSTATES*2
        // branch_table: NUMSTATES/2 * sizeof(s16)                      = NUMSTATES  
        // decision:     NUMSTATES/DECISION_BITSIZE * DECISION_BYTESIZE = NUMSTATES/8
        // 
        // m128_metric_width:       NUMSTATES*2 / sizeof(__m128i) = NUMSTATES/8
        // m128_branch_table_width: NUMSTATES   / sizeof(__m128i) = NUMSTATES/16
        // u16_decision_width:      NUMSTATES/8 / sizeof(u16)     = NUMSTATES/16
        m128_width_metric(this->NUMSTATES/ALIGN_AMOUNT*2u),
        m128_width_branch_table(this->NUMSTATES/ALIGN_AMOUNT),
        u16_width_decision(this->NUMSTATES/ALIGN_AMOUNT),
        m128_symbols(this->R)
    {
        assert(this->K >= K_min);
        // Metrics must meet alignment requirements
        assert((this->METRIC_LENGTH * sizeof(uint16_t)) % ALIGN_AMOUNT == 0);
        assert((this->METRIC_LENGTH * sizeof(uint16_t)) >= ALIGN_AMOUNT);
        // Branch table must be meet alignment requirements 
        assert(this->branch_table.alignment % ALIGN_AMOUNT == 0);
        assert(this->branch_table.alignment >= ALIGN_AMOUNT);

        assert(((uintptr_t)m128_symbols.data() % ALIGN_AMOUNT) == 0);
    }

    inline
    void update(const int16_t* symbols, const size_t N) {
        // number of symbols must be a multiple of the code rate
        assert(N % this->R == 0);
        const size_t total_decoded_bits = N / this->R;
        const size_t max_decoded_bits = this->get_traceback_length() + this->TOTAL_STATE_BITS;
        assert((total_decoded_bits + curr_decoded_bit) <= max_decoded_bits);

        for (size_t s = 0; s < N; s+=this->R) {
            auto* decision = this->get_decision(this->curr_decoded_bit);
            auto* old_metric = this->get_old_metric();
            auto* new_metric = this->get_new_metric();
            bfly(&symbols[s], decision, old_metric, new_metric);
            if (new_metric[0] >= this->config.renormalisation_threshold) {
                renormalise(new_metric);
            }
            this->swap_metrics();
            this->curr_decoded_bit++;
        }
    }
private:
    inline
    void bfly(const int16_t* symbols, uint16_t* decision, uint16_t* old_metric, uint16_t* new_metric) 
    {
        const __m128i* m128_branch_table = reinterpret_cast<const __m128i*>(this->branch_table.data());
        __m128i* m128_old_metric = reinterpret_cast<__m128i*>(old_metric);
        __m128i* m128_new_metric = reinterpret_cast<__m128i*>(new_metric);

        assert(((uintptr_t)m128_branch_table % ALIGN_AMOUNT) == 0);
        assert(((uintptr_t)m128_old_metric % ALIGN_AMOUNT) == 0);
        assert(((uintptr_t)m128_new_metric % ALIGN_AMOUNT) == 0);

        // Vectorise constants
        for (size_t i = 0; i < this->R; i++) {
            m128_symbols[i] = _mm_set1_epi16(symbols[i]);
        }
        const __m128i max_error = _mm_set1_epi16(this->config.soft_decision_max_error);

        for (size_t curr_state = 0u; curr_state < m128_width_branch_table; curr_state++) {
            // Total errors across R symbols
            __m128i total_error = _mm_set1_epi16(0);
            for (size_t i = 0u; i < this->R; i++) {
                __m128i error = _mm_subs_epi16(m128_branch_table[i*m128_width_branch_table+curr_state], m128_symbols[i]);
                error = _mm_abs_epi16(error);
                total_error = _mm_adds_epu16(total_error, error);
            }

            // Butterfly algorithm
            const __m128i m_total_error = _mm_subs_epu16(max_error, total_error);
            const __m128i m0 = _mm_adds_epu16(m128_old_metric[curr_state                      ],   total_error);
            const __m128i m1 = _mm_adds_epu16(m128_old_metric[curr_state + m128_width_metric/2], m_total_error);
            const __m128i m2 = _mm_adds_epu16(m128_old_metric[curr_state                      ], m_total_error);
            const __m128i m3 = _mm_adds_epu16(m128_old_metric[curr_state + m128_width_metric/2],   total_error);
            const __m128i survivor0 = _mm_min_epu16(m0, m1);
            const __m128i survivor1 = _mm_min_epu16(m2, m3);
            const __m128i decision0 = _mm_cmpeq_epi16(survivor0, m1);
            const __m128i decision1 = _mm_cmpeq_epi16(survivor1, m3);

            // Update metrics
            // https://www.intel.com/content/www/us/en/docs/intrinsics-guide/index.html#text=_mm_unpacklo_epi16&ig_expand=7385
            // _mm_unpacklo_epi16(a,b) = Unpacks and interleave 16-bit integers from the low half of a and b.
            // c[0:127] = a[0:15],b[0:15],a[16:32],b[16:31],...,a[48:63],b[48:63]
            m128_new_metric[2*curr_state+0] = _mm_unpacklo_epi16(survivor0, survivor1);
            m128_new_metric[2*curr_state+1] = _mm_unpackhi_epi16(survivor0, survivor1);

            // Pack each set of decisions into 8 8-bit bytes, then interleave them and compress into 16 bits
            // https://www.intel.com/content/www/us/en/docs/intrinsics-guide/index.html#text=_mm_packs_epi16&ig_expand=5136
            // _mm_packs_epi16(a,b) = Converts packed 16-bit from a and b to packed 8-bit using signed saturation
            // a is saturated to 8bits then placed at bits 63:0
            // b is saturated to 8bits then placed at bits 127:64
            // https://www.intel.com/content/www/us/en/docs/intrinsics-guide/index.html#text=_mm_unpacklo_epi8
            // _mm_unpacklo_epi8(a,b) = Unpacks and interleave 8-bit integers from the low half of a and b.
            // c[0:127] = a[0:7],b[0:7],a[8:15],b[8:15],...,a[56:63],b[56:63]
            // https://www.intel.com/content/www/us/en/docs/intrinsics-guide/index.html#text=_mm_movemask_epi8
            // _mm_movemask_epi8(a) = Returns a mask of the most significant bit of each element in a.
            // c[0:15] = a[7],a[15],a[23],...,a[127]
            decision[curr_state] = (uint16_t)_mm_movemask_epi8(_mm_unpacklo_epi8(
                _mm_packs_epi16(decision0, _mm_setzero_si128()), 
                _mm_packs_epi16(decision1, _mm_setzero_si128())));
        }
    }

    inline
    void renormalise(uint16_t* metric) {
        assert(((uintptr_t)metric % ALIGN_AMOUNT) == 0);
        __m128i* m128_metric = reinterpret_cast<__m128i*>(metric);

        // Find minimum  
        __m128i adjustv = m128_metric[0];
        for (size_t i = 1u; i < m128_width_metric; i++) {
            adjustv = _mm_min_epu16(adjustv, m128_metric[i]);
        }

        // Shift half of the array onto the other half and get the minimum between them
        // Repeat this until we get the minimum value of all 16bit values
        // NOTE: srli performs shift on 128bit lanes
        adjustv = _mm_min_epu16(adjustv, _mm_srli_si128(adjustv, 8));
        adjustv = _mm_min_epu16(adjustv, _mm_srli_si128(adjustv, 4));
        adjustv = _mm_min_epu16(adjustv, _mm_srli_si128(adjustv, 2));

        // Normalise to minimum
        const uint16_t* reduce_buffer = reinterpret_cast<uint16_t*>(&adjustv);
        const uint16_t min = reduce_buffer[0];
        const __m128i vmin = _mm_set1_epi16(min);
        for (size_t i = 0u; i < m128_width_metric; i++) {
            m128_metric[i] = _mm_subs_epu16(m128_metric[i], vmin);
        }

        // Keep track of absolute error metrics
        this->renormalisation_bias += absolute_error_t(min);
    }
};
