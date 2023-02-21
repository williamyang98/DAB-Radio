/* Generic Viterbi decoder,
 * Copyright Phil Karn, KA9Q,
 * Karn's original code can be found here: https://github.com/ka9q/libfec 
 * May be used under the terms of the GNU Lesser General Public License (LGPL)
 * see http://www.gnu.org/copyleft/lgpl.html
 */
#pragma once
#include "viterbi_branch_table.h"

#include <stdint.h>
#include <stddef.h>
#include <vector>
#include <assert.h>
#include "utility/aligned_vector.h"
#include "utility/basic_ops.h"

// User configurable constants for decoder
template <typename error_t>
struct ViterbiDecoder_Config 
{
    error_t soft_decision_max_error;            // max total error for R output symbols against reference
    error_t initial_start_error;
    error_t initial_non_start_error;
    error_t renormalisation_threshold;          // threshold to normalise all errors to 0
};

// Core data structures for viterbi decoder
// Traceback technique is the same for all types of viterbi decoders
template <
    typename error_t,
    typename soft_t, 
    typename decision_bits_t,
    typename absolute_error_t 
>
class ViterbiDecoder_Core
{
public:
    const size_t K;
    const size_t R;
protected:
    static constexpr size_t DECISIONTYPE_BITSIZE = sizeof(decision_bits_t) * 8u;
    const size_t NUMSTATES;
    const size_t TOTAL_STATE_BITS;
    const size_t DECISION_BITS_LENGTH;
    const size_t METRIC_LENGTH;

    const ViterbiBranchTable<soft_t>& branch_table;   // we can reuse an existing branch table
    AlignedVector<error_t> metrics;             // shape: (2 x METRIC_LENGTH)      
    size_t curr_metric_index;                   // 0/1 to swap old and new metrics
    std::vector<decision_bits_t> decisions;     // shape: (TRACEBACK_LENGTH x DECISION_BITS_LENGTH)
    size_t curr_decoded_bit;

    const ViterbiDecoder_Config<error_t> config;
    absolute_error_t renormalisation_bias;      // keep track of the absolute error when we renormalise error_t
public:
    ViterbiDecoder_Core(
        const ViterbiBranchTable<soft_t>& _branch_table,
        const ViterbiDecoder_Config<error_t>& _config)
    :   K(_branch_table.K),
        R(_branch_table.R),
        // size of various data structures
        NUMSTATES(std::size_t(1) << (K-1u)),
        TOTAL_STATE_BITS(K-1u),
        DECISION_BITS_LENGTH(max(NUMSTATES/DECISIONTYPE_BITSIZE, std::size_t(1u))),
        METRIC_LENGTH(NUMSTATES),
        // internal data structures
        branch_table(_branch_table),
        metrics(2u*METRIC_LENGTH, _branch_table.alignment),  
        decisions(),
        config(_config)
    {
        assert(K > 1u);       
        assert(R > 1u);
        reset();
        set_traceback_length(0);
    }

    // Traceback length doesn't include tail bits
    void set_traceback_length(const size_t traceback_length) {
        const size_t new_length = traceback_length + TOTAL_STATE_BITS;
        decisions.resize(new_length * DECISION_BITS_LENGTH);
        if (curr_decoded_bit > new_length) {
            curr_decoded_bit = new_length;
        }
    }

    size_t get_traceback_length() const {
        const size_t N = decisions.size();
        const size_t M = N / DECISION_BITS_LENGTH;
        return M - TOTAL_STATE_BITS;
    }

    size_t get_current_decoded_bit() const {
        return curr_decoded_bit;
    }

    void reset(const size_t starting_state = 0u) {
        curr_metric_index = 0u;
        curr_decoded_bit = 0u;
        renormalisation_bias = 0u;
        auto* old_metric = get_old_metric();
        for (size_t i = 0; i < METRIC_LENGTH; i++) {
            old_metric[i] = config.initial_non_start_error;
        }
        const size_t STATE_MASK = NUMSTATES-1u;
        old_metric[starting_state & STATE_MASK] = config.initial_start_error;
        std::memset(decisions.data(), 0, decisions.size()*sizeof(decision_bits_t));
    }

    absolute_error_t chainback(uint8_t* bytes_out, const size_t total_bits, const size_t end_state = 0u) {
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

        auto* old_metric = get_old_metric();
        const error_t normalised_error = old_metric[end_state % NUMSTATES];
        return renormalisation_bias + absolute_error_t(normalised_error);
    }
protected:
    // swap old and new metrics
    inline
    error_t* get_new_metric() { 
        return &metrics[curr_metric_index]; 
    }

    inline
    error_t* get_old_metric() { 
        return &metrics[METRIC_LENGTH-curr_metric_index]; 
    }

    inline
    void swap_metrics() { 
        curr_metric_index = METRIC_LENGTH-curr_metric_index; 
    }

    inline
    decision_bits_t* get_decision(const size_t i) {
        return &decisions[i*DECISION_BITS_LENGTH];
    }
private:
    // align curr_state so we get output bytes
    std::pair<size_t, size_t> get_shift() {
        const size_t M = K-1;
        if (M < 8) {
            return { 8-M, 0 };
        } else if (M > 8) {
            return { 0, M-8 };
        } else {
            return { 0, 0 };
        }
    };
};