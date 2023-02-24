/* Generic Viterbi decoder,
 * Copyright Phil Karn, KA9Q,
 * Karn's original code can be found here: https://github.com/ka9q/libfec 
 * May be used under the terms of the GNU Lesser General Public License (LGPL)
 * see http://www.gnu.org/copyleft/lgpl.html
 */
#pragma once
#include "viterbi_branch_table.h"
#include "viterbi_decoder_config.h"

#include <stdint.h>
#include <stddef.h>
#include <vector>
#include <cstring>
#include <assert.h>

#ifdef _MSC_VER
#define ALIGNED(x) __declspec(align(x))
#else
#define ALIGNED(x) __attribute__ ((aligned(x)))
#endif

// Core data structures for viterbi decoder
// Traceback technique is the same for all types of viterbi decoders
template <
    size_t constraint_length, size_t code_rate,
    typename error_t,
    typename soft_t, 
    typename decision_bits_t
>
class ViterbiDecoder_Core
{
private:
    static constexpr 
    size_t get_alignment(const size_t x) {
        if (x % 32 == 0) {
            return 32;
        } else if (x % 16 == 0) {
            return 16;
        } else {
            return x;
        }
    }
public:
    static constexpr size_t K = constraint_length;
    static constexpr size_t R = code_rate;
protected:
    static constexpr size_t DECISIONTYPE_BITSIZE = sizeof(decision_bits_t) * 8u;
    static constexpr size_t NUMSTATES = size_t(1u) << (K-1u);
    static constexpr size_t TOTAL_STATE_BITS = K-1u;
    static constexpr size_t DECISION_BITS_LENGTH = (NUMSTATES/DECISIONTYPE_BITSIZE > size_t(1u)) ? (NUMSTATES/DECISIONTYPE_BITSIZE) : size_t(1u);
    static constexpr size_t METRIC_LENGTH = NUMSTATES;
    static constexpr size_t METRIC_ALIGNMENT = get_alignment(sizeof(error_t)*METRIC_LENGTH);

    struct metric_t {
        error_t buf[METRIC_LENGTH];
    } ALIGNED(METRIC_ALIGNMENT);

    struct decision_branch_t {
        decision_bits_t buf[DECISION_BITS_LENGTH];
    };

    const ViterbiBranchTable<K,R,soft_t>& branch_table;   // we can reuse an existing branch table
    const ViterbiDecoder_Config<error_t> config;

    ALIGNED(METRIC_ALIGNMENT) metric_t metrics[2];
    size_t curr_metric_index;                   // 0/1 to swap old and new metrics
    std::vector<decision_branch_t> decisions;     // shape: (TRACEBACK_LENGTH x DECISION_BITS_LENGTH)
    size_t curr_decoded_bit;
public:
    ViterbiDecoder_Core(
        const ViterbiBranchTable<K,R,soft_t>& _branch_table,
        const ViterbiDecoder_Config<error_t>& _config)
    :   branch_table(_branch_table),
        config(_config),
        decisions()
    {
        static_assert(K >= 2u);       
        static_assert(R >= 1u);
        static_assert(sizeof(metric_t) % METRIC_ALIGNMENT == 0);
        assert(uintptr_t(&metrics[0].buf[0]) % METRIC_ALIGNMENT == 0);
        reset();
        set_traceback_length(0);
    }

    // Traceback length doesn't include tail bits
    void set_traceback_length(const size_t traceback_length) {
        const size_t new_length = traceback_length + TOTAL_STATE_BITS;
        decisions.resize(new_length);
        if (curr_decoded_bit > new_length) {
            curr_decoded_bit = new_length;
        }
    }

    size_t get_traceback_length() const {
        const size_t N = decisions.size();
        return N - TOTAL_STATE_BITS;
    }

    size_t get_current_decoded_bit() const {
        return curr_decoded_bit;
    }

    void reset(const size_t starting_state = 0u) {
        curr_metric_index = 0u;
        curr_decoded_bit = 0u;
        auto* old_metric = get_old_metric();
        for (size_t i = 0; i < METRIC_LENGTH; i++) {
            old_metric[i] = config.initial_non_start_error;
        }
        const size_t STATE_MASK = NUMSTATES-1u;
        old_metric[starting_state & STATE_MASK] = config.initial_start_error;
        std::memset(decisions.data(), 0, decisions.size()*sizeof(decision_bits_t));
    }

    void chainback(uint8_t* bytes_out, const size_t total_bits, const size_t end_state = 0u) {
        const size_t TRACEBACK_LENGTH = get_traceback_length();
        const auto [ADDSHIFT, SUBSHIFT] = get_shift();
        assert(TRACEBACK_LENGTH >= total_bits);
        assert((curr_decoded_bit - TOTAL_STATE_BITS) == total_bits);

        size_t curr_state = end_state;
        curr_state = (curr_state % NUMSTATES) << ADDSHIFT;

        for (size_t i = 0u; i < total_bits; i++) {
            const size_t j = (total_bits-1)-i;
            const size_t curr_decoded_byte = j/8;
            const size_t curr_decision = j + TOTAL_STATE_BITS;

            const size_t curr_pack_index = (curr_state >> ADDSHIFT) / DECISIONTYPE_BITSIZE;
            const size_t curr_pack_bit   = (curr_state >> ADDSHIFT) % DECISIONTYPE_BITSIZE;

            auto* decision = get_decision(curr_decision);
            const size_t input_bit = (decision[curr_pack_index] >> curr_pack_bit) & 0b1;

            curr_state = (curr_state >> 1) | (input_bit << (K-2+ADDSHIFT));
            bytes_out[curr_decoded_byte] = (uint8_t)(curr_state >> SUBSHIFT);
        }
    }
protected:
    // swap old and new metrics
    inline
    error_t* get_new_metric() { 
        return &(metrics[curr_metric_index].buf[0]); 
    }

    inline
    error_t* get_old_metric() { 
        return &(metrics[1-curr_metric_index].buf[0]); 
    }

    inline
    void swap_metrics() { 
        curr_metric_index = 1-curr_metric_index; 
    }

    inline
    decision_bits_t* get_decision(const size_t i) {
        return &decisions[i].buf[0];
    }
private:
    // align curr_state so we get output bytes
    constexpr std::pair<size_t, size_t> get_shift() {
        constexpr size_t M = K-1;
        if (M < 8) {
            return { 8-M, 0 };
        } else if (M > 8) {
            return { 0, M-8 };
        } else {
            return { 0, 0 };
        }
    };
};

#undef ALIGNED