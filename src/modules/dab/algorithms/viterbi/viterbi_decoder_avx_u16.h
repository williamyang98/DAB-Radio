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

// Vectorisation using AVX
//     16bit integers for errors, soft-decision values
//     16 way vectorisation from 256bits/16bits 
//     32bit decision type since 16 x 2 decisions bits per branch
template <typename absolute_error_t = uint64_t>
class ViterbiDecoder_AVX_u16: public ViterbiDecoder_Core<uint16_t, int16_t, uint32_t, absolute_error_t>
{
private:
    using Base = ViterbiDecoder_Core<uint16_t, int16_t, uint32_t, absolute_error_t>;
public:
    static constexpr size_t ALIGN_AMOUNT = sizeof(__m256i);
    static constexpr size_t K_min = 6;
private:
    const size_t m256_width_metric;
    const size_t m256_width_branch_table;
    const size_t u32_width_decision; 
    std::vector<__m256i> m256_symbols;
public:
    // NOTE: branch_table.K >= 6 and branch_table.alignment >= 32  
    template <typename ... U>
    ViterbiDecoder_AVX_u16(U&& ... args)
    :   Base(std::forward<U>(args)...),
        // metric:       NUMSTATES   * sizeof(u16)                      = NUMSTATES*2
        // branch_table: NUMSTATES/2 * sizeof(s16)                      = NUMSTATES  
        // decision:     NUMSTATES/DECISION_BITSIZE * DECISION_BYTESIZE = NUMSTATES/8
        // 
        // m256_metric_width:       NUMSTATES*2 / sizeof(__m256i) = NUMSTATES/16
        // m256_branch_table_width: NUMSTATES   / sizeof(__m256i) = NUMSTATES/32
        // u32_decision_width:      NUMSTATES/8 / sizeof(u32)     = NUMSTATES/32
        m256_width_metric(this->NUMSTATES/ALIGN_AMOUNT*2u),
        m256_width_branch_table(this->NUMSTATES/ALIGN_AMOUNT),
        u32_width_decision(this->NUMSTATES/ALIGN_AMOUNT),
        m256_symbols(this->R)
    {
        assert(this->K >= K_min);
        // Metrics must meet alignment requirements
        assert((this->METRIC_LENGTH * sizeof(uint16_t)) % ALIGN_AMOUNT == 0);
        assert((this->METRIC_LENGTH * sizeof(uint16_t)) >= ALIGN_AMOUNT);
        // Branch table must be meet alignment requirements 
        assert(this->branch_table.alignment % ALIGN_AMOUNT == 0);
        assert(this->branch_table.alignment >= ALIGN_AMOUNT);

        assert(((uintptr_t)m256_symbols.data() % ALIGN_AMOUNT) == 0);
    }

    inline
    void update(const int16_t* symbols, const size_t N) {
        // number of symbols must be a multiple of the code rate
        assert(N % this->R == 0);
        const size_t total_decoded_bits = N / this->R;
        const size_t max_decoded_bits = this->get_traceback_length() + this->TOTAL_STATE_BITS;
        assert((total_decoded_bits + curr_decoded_bit) <= max_decoded_bits);

        for (size_t s = 0; s < N; s+=(this->R)) {
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
    void bfly(const int16_t* symbols, uint32_t* decision, uint16_t* old_metric, uint16_t* new_metric) 
    {
        const __m256i* m256_branch_table = reinterpret_cast<const __m256i*>(this->branch_table.data());
        __m256i* m256_old_metric = reinterpret_cast<__m256i*>(old_metric);
        __m256i* m256_new_metric = reinterpret_cast<__m256i*>(new_metric);

        assert(((uintptr_t)m256_branch_table % ALIGN_AMOUNT) == 0);
        assert(((uintptr_t)m256_old_metric % ALIGN_AMOUNT) == 0);
        assert(((uintptr_t)m256_new_metric % ALIGN_AMOUNT) == 0);

        // Vectorise constants
        for (size_t i = 0; i < this->R; i++) {
            m256_symbols[i] = _mm256_set1_epi16(symbols[i]);
        }
        const __m256i max_error = _mm256_set1_epi16(this->config.soft_decision_max_error);

        for (size_t curr_state = 0u; curr_state < m256_width_branch_table; curr_state++) {
            // Total errors across R symbols
            __m256i total_error = _mm256_set1_epi16(0);
            for (size_t i = 0u; i < this->R; i++) {
                __m256i error = _mm256_subs_epi16(m256_branch_table[i*m256_width_branch_table+curr_state], m256_symbols[i]);
                error = _mm256_abs_epi16(error);
                total_error = _mm256_adds_epu16(total_error, error);
            }

            // Butterfly algorithm
            const __m256i m_total_error = _mm256_subs_epu16(max_error, total_error);
            const __m256i m0 = _mm256_adds_epu16(m256_old_metric[curr_state                      ],   total_error);
            const __m256i m1 = _mm256_adds_epu16(m256_old_metric[curr_state + m256_width_metric/2], m_total_error);
            const __m256i m2 = _mm256_adds_epu16(m256_old_metric[curr_state                      ], m_total_error);
            const __m256i m3 = _mm256_adds_epu16(m256_old_metric[curr_state + m256_width_metric/2],   total_error);
            const __m256i survivor0 = _mm256_min_epu16(m0, m1);
            const __m256i survivor1 = _mm256_min_epu16(m2, m3);
            const __m256i decision0 = _mm256_cmpeq_epi16(survivor0, m1);
            const __m256i decision1 = _mm256_cmpeq_epi16(survivor1, m3);

            // Update metrics
            const __m256i new_metric_lo = _mm256_unpacklo_epi16(survivor0, survivor1);
            const __m256i new_metric_hi = _mm256_unpackhi_epi16(survivor0, survivor1);
            // Reshuffle into correct order along 128bit boundaries
            m256_new_metric[2*curr_state+0] = _mm256_permute2x128_si256(new_metric_lo, new_metric_hi, 0b0010'0000);
            m256_new_metric[2*curr_state+1] = _mm256_permute2x128_si256(new_metric_lo, new_metric_hi, 0b0011'0001);

            // Pack each set of decisions into 8 8-bit bytes, then interleave them and compress into 16 bits
            // 256bit packs works with 128bit segments
            // 256bit unpack works with 128bit segments
            // | = 128bit boundary
            // packs_16  : d0 .... 0 .... | d1 .... 0 ....
            // packs_16  : d2 .... 0 .... | d3 .... 0 ....
            // unpacklo_8: d0 d2 d0 d2 .. | d1 d3 d1 d3 ..
            // movemask_8: b0 b2 b0 b2 .. | b1 b3 b1 b3 ..
            decision[curr_state] = _mm256_movemask_epi8(_mm256_unpacklo_epi8(
                _mm256_packs_epi16(decision0, _mm256_setzero_si256()), 
                _mm256_packs_epi16(decision1, _mm256_setzero_si256())));
        }
    }

    inline
    void renormalise(uint16_t* metric) {
        assert(((uintptr_t)metric % ALIGN_AMOUNT) == 0);
        __m256i* m256_metric = reinterpret_cast<__m256i*>(metric);

        union {
            __m256i m256;
            __m128i m128[2];
            uint16_t u16[16]; 
        } reduce_buffer;

        // Find minimum 
        reduce_buffer.m256 = m256_metric[0];
        for (size_t i = 1u; i < m256_width_metric; i++) {
            reduce_buffer.m256 = _mm256_min_epu16(reduce_buffer.m256, m256_metric[i]);
        }

        // Shift half of the array onto the other half and get the minimum between them
        // Repeat this until we get the minimum value of all 16bit values
        // NOTE: srli performs shift on 128bit lanes
        __m128i adjustv = _mm_min_epu16(reduce_buffer.m128[0], reduce_buffer.m128[1]);
        adjustv = _mm_min_epu16(adjustv, _mm_srli_si128(adjustv, 8));
        adjustv = _mm_min_epu16(adjustv, _mm_srli_si128(adjustv, 4));
        adjustv = _mm_min_epu16(adjustv, _mm_srli_si128(adjustv, 2));
        reduce_buffer.m128[0] = adjustv;

        // Normalise to minimum
        const uint16_t min = reduce_buffer.u16[0];
        const __m256i vmin = _mm256_set1_epi16(min);
        for (size_t i = 0u; i < m256_width_metric; i++) {
            m256_metric[i] = _mm256_subs_epu16(m256_metric[i], vmin);
        }

        // Keep track of absolute error metrics
        this->renormalisation_bias += absolute_error_t(min);
    }
};