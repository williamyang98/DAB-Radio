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
template <size_t constraint_length, size_t code_rate>
class ViterbiDecoder_SSE_u16: public ViterbiDecoder_Core<constraint_length, code_rate, uint16_t, int16_t, uint16_t>
{
private:
    using Base = ViterbiDecoder_Core<constraint_length, code_rate, uint16_t, int16_t, uint16_t>;
private:
    // metric:       NUMSTATES   * sizeof(u16)                      = NUMSTATES*2
    // branch_table: NUMSTATES/2 * sizeof(s16)                      = NUMSTATES  
    // decision:     NUMSTATES/DECISION_BITSIZE * DECISION_BYTESIZE = NUMSTATES/8
    // 
    // m256_metric_width:       NUMSTATES*2 / sizeof(__m128i) = NUMSTATES/8
    // m256_branch_table_width: NUMSTATES   / sizeof(__m128i) = NUMSTATES/16
    // u32_decision_width:      NUMSTATES/8 / sizeof(u16)     = NUMSTATES/16
    static constexpr size_t ALIGN_AMOUNT = sizeof(__m128i);
    static constexpr size_t m128_width_metric = Base::NUMSTATES/ALIGN_AMOUNT*2u;
    static constexpr size_t m128_width_branch_table = Base::NUMSTATES/ALIGN_AMOUNT;
    static constexpr size_t u16_width_decision = Base::NUMSTATES/ALIGN_AMOUNT; 
    static constexpr size_t K_min = 5;
    uint64_t renormalisation_bias;
public:
    static constexpr bool is_valid = Base::K >= K_min;

    template <typename ... U>
    ViterbiDecoder_SSE_u16(U&& ... args)
    :   Base(std::forward<U>(args)...)
    {
        static_assert(is_valid, "Insufficient constraint length for vectorisation");
        static_assert(sizeof(typename Base::metric_t) % ALIGN_AMOUNT == 0);
    }

    inline
    uint64_t get_error(const size_t end_state=0u) {
        auto* old_metric = Base::get_old_metric();
        const uint16_t normalised_error = old_metric[end_state % Base::NUMSTATES];
        return renormalisation_bias + uint64_t(normalised_error);
    }

    inline
    void reset(const size_t starting_state = 0u) {
        Base::reset(starting_state);
        renormalisation_bias = uint64_t(0u);
    }

    inline
    void update(const int16_t* symbols, const size_t N) {
        // number of symbols must be a multiple of the code rate
        assert(N % Base::R == 0);
        const size_t total_decoded_bits = N / Base::R;
        const size_t max_decoded_bits = Base::get_traceback_length() + Base::TOTAL_STATE_BITS;
        assert((total_decoded_bits + Base::curr_decoded_bit) <= max_decoded_bits);

        for (size_t s = 0; s < N; s+=(Base::R)) {
            auto* decision = Base::get_decision(Base::curr_decoded_bit);
            auto* old_metric = Base::get_old_metric();
            auto* new_metric = Base::get_new_metric();
            bfly(&symbols[s], decision, old_metric, new_metric);
            if (new_metric[0] >= Base::config.renormalisation_threshold) {
                renormalise(new_metric);
            }
            Base::swap_metrics();
            Base::curr_decoded_bit++;
        }
    }
private:
    inline
    void bfly(const int16_t* symbols, uint16_t* decision, uint16_t* old_metric, uint16_t* new_metric) 
    {
        const __m128i* m128_branch_table = reinterpret_cast<const __m128i*>(Base::branch_table.data());
        __m128i* m128_old_metric = reinterpret_cast<__m128i*>(old_metric);
        __m128i* m128_new_metric = reinterpret_cast<__m128i*>(new_metric);

        assert(((uintptr_t)m128_branch_table % ALIGN_AMOUNT) == 0);
        assert(((uintptr_t)m128_old_metric % ALIGN_AMOUNT) == 0);
        assert(((uintptr_t)m128_new_metric % ALIGN_AMOUNT) == 0);

        __m128i m128_symbols[Base::R];

        // Vectorise constants
        for (size_t i = 0; i < Base::R; i++) {
            m128_symbols[i] = _mm_set1_epi16(symbols[i]);
        }
        const __m128i max_error = _mm_set1_epi16(Base::config.soft_decision_max_error);

        for (size_t curr_state = 0u; curr_state < m128_width_branch_table; curr_state++) {
            // Total errors across R symbols
            __m128i total_error = _mm_set1_epi16(0);
            for (size_t i = 0u; i < Base::R; i++) {
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
            m128_new_metric[2*curr_state+0] = _mm_unpacklo_epi16(survivor0, survivor1);
            m128_new_metric[2*curr_state+1] = _mm_unpackhi_epi16(survivor0, survivor1);

            // Pack each set of decisions into 8 8-bit bytes, then interleave them and compress into 16 bits
            decision[curr_state] = _mm_movemask_epi8(_mm_unpacklo_epi8(
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
        renormalisation_bias += uint64_t(min);
    }
};