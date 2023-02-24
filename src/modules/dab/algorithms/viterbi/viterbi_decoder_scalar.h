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
#include <vector>
#include <assert.h>

template <
    size_t constraint_length, size_t code_rate,
    typename error_t,               
    typename soft_t,                 
    typename decision_bits_t = uint64_t,
    typename absolute_error_t = uint64_t       
>
class ViterbiDecoder_Scalar: public ViterbiDecoder_Core<constraint_length, code_rate, error_t, soft_t, decision_bits_t>
{
private:
    using Base = ViterbiDecoder_Core<constraint_length, code_rate, error_t, soft_t, decision_bits_t>;    
    static constexpr size_t K_min = 2;
    absolute_error_t renormalisation_bias;
public:
    static constexpr bool is_valid = Base::K >= K_min;

    template <typename ... U>
    ViterbiDecoder_Scalar(U&& ... args)
    :   Base(std::forward<U>(args)...)
    {
        static_assert(is_valid, "Scalar decoder must have constraint length of at least 2");
    }

    inline
    absolute_error_t get_error(const size_t end_state=0u) {
        auto* old_metric = Base::get_old_metric();
        const error_t normalised_error = old_metric[end_state % Base::NUMSTATES];
        return renormalisation_bias + absolute_error_t(normalised_error);
    }

    inline
    void reset(const size_t starting_state = 0u) {
        Base::reset(starting_state);
        renormalisation_bias = absolute_error_t(0u);
    }

    // NOTE: We expect the symbol values to be in the range set by the branch_table
    //       symbols[i] ∈ [soft_decision_low, soft_decision_high]
    //       Otherwise when we calculate inside bfly(...):
    //           m_total_error = soft_decision_max_error - total_error
    //       The resulting value could underflow with unsigned error types 
    inline
    void update(const soft_t* symbols, const size_t N) {
        // number of symbols must be a multiple of the code rate
        assert(N % Base::R == 0);
        const size_t total_decoded_bits = N / Base::R;
        const size_t max_decoded_bits = Base::get_traceback_length() + Base::TOTAL_STATE_BITS;
        assert((total_decoded_bits + Base::curr_decoded_bit) <= max_decoded_bits);

        for (size_t i = 0u; i < N; i+=(Base::R)) {
            auto* decision = Base::get_decision(Base::curr_decoded_bit);
            auto* old_metric = Base::get_old_metric();
            auto* new_metric = Base::get_new_metric();
            bfly(&symbols[i], decision, old_metric, new_metric);
            if (new_metric[0] >= Base::config.renormalisation_threshold) {
                renormalise(new_metric);
            }
            Base::swap_metrics();
            Base::curr_decoded_bit++;
        }
    }
private:
    // Process R symbols for 1 decoded bit
    inline 
    void bfly(const soft_t* symbols, decision_bits_t* decision, error_t* old_metric, error_t* new_metric) {
        for (size_t curr_state = 0u; curr_state < Base::branch_table.stride; curr_state++) {
            // Error associated with state given symbols
            error_t total_error = 0u;
            for (size_t i = 0; i < Base::R; i++) {
                const soft_t sym = symbols[i];
                const soft_t expected_sym = Base::branch_table[i][curr_state];
                const soft_t error = expected_sym - sym;
                const error_t abs_error = error_t(abs(error));
                total_error += abs_error;
            }
            assert(total_error <= Base::config.soft_decision_max_error);

            // Butterfly algorithm
            const error_t m_total_error = Base::config.soft_decision_max_error - total_error;
            // TODO: When adding our error metrics we may cause an overflow to happen 
            //       if the renormalisation step was not performed in time
            //       Our intrinsics implementations use saturated arithmetic to prevent an overflow
            //       Perhaps it is possible to use something like GCC's builtin saturated add here?
            const error_t m0 = old_metric[curr_state                        ] +   total_error;
            const error_t m1 = old_metric[curr_state + Base::METRIC_LENGTH/2] + m_total_error;
            const error_t m2 = old_metric[curr_state                        ] + m_total_error;
            const error_t m3 = old_metric[curr_state + Base::METRIC_LENGTH/2] +   total_error;
            const decision_bits_t d0 = m0 > m1;
            const decision_bits_t d1 = m2 > m3;

            // Update metrics
            new_metric[2*curr_state+0] = d0 ? m1 : m0;
            new_metric[2*curr_state+1] = d1 ? m3 : m2;

            // Pack decision bits
            const decision_bits_t bits = d0 | (d1 << 1);
            constexpr size_t total_decision_bits = 2u;
            const size_t curr_bit_index = curr_state*total_decision_bits;
            const size_t curr_pack_index = curr_bit_index / Base::DECISIONTYPE_BITSIZE;
            const size_t curr_pack_bit   = curr_bit_index % Base::DECISIONTYPE_BITSIZE;
            decision[curr_pack_index] |= (bits << curr_pack_bit);
        }
    }

    // Normalise error metrics so minimum value is 0
    inline 
    void renormalise(error_t* metric) {
        error_t min = metric[0];
        for (size_t curr_state = 1u; curr_state < Base::METRIC_LENGTH; curr_state++) {
            error_t x = metric[curr_state];
            if (x < min) {
                min = x;
            }
        }

        for (size_t curr_state = 0u; curr_state < Base::METRIC_LENGTH; curr_state++) {
            error_t& x = metric[curr_state];
            x -= min;
        }

        renormalisation_bias += absolute_error_t(min);
    }
};