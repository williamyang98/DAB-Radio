#pragma once

#include <stdint.h>
#include <stddef.h>
#include <assert.h>
#include "utility/parity_table.h"

#ifdef _MSC_VER
#define ALIGNED(x) __declspec(align(x))
#else
#define ALIGNED(x) __attribute__ ((aligned(x)))
#endif

// If the same parameters are used for the viterbi decoder
// We can reuse the branch table for better memory usage
template <size_t constraint_length, size_t code_rate, typename soft_t>
class ViterbiBranchTable 
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
    static constexpr size_t stride = (size_t(1) << (K-2u));
    static constexpr size_t alignment = get_alignment(sizeof(soft_t)*stride);
    struct branch_t {
        soft_t buf[stride];
    } ALIGNED(alignment);
private:
    const soft_t soft_decision_high;            // soft value for high symbol
    const soft_t soft_decision_low;             // soft value for low symbol
    ALIGNED(alignment) branch_t branch_table[R];
public:
    // NOTE: Polynomials (G) should be in binary form with least signficant bit corresponding to the input bit
    template <typename code_t>
    ViterbiBranchTable(
        const code_t* G,                        // length = code_rate
        const soft_t _soft_decision_high,
        const soft_t _soft_decision_low)
    :   soft_decision_high(_soft_decision_high),
        soft_decision_low(_soft_decision_low)
    {
        static_assert(K > 1u);       
        static_assert(R > 1u);
        static_assert(sizeof(branch_t) % alignment == 0);
        assert(uintptr_t(this->data()) % alignment == 0);
        assert(soft_decision_high > soft_decision_low);
        calculate_branch_table(G);
    }

    // Index like a 2D array
    inline
    soft_t* operator[](size_t index) { 
        assert(index < R);
        return &branch_table[index].buf[0];
    }

    inline
    const soft_t* operator[](size_t index) const { 
        assert(index < R);
        return &branch_table[index].buf[0];
    }

    auto data() const { return &branch_table[0].buf[0]; }
private:
    template <typename code_t>
    void calculate_branch_table(const code_t* G) {
        auto& parity_table = ParityTable::get();
        for (size_t state = 0u; state < stride; state++) {
            for (size_t i = 0u; i < R; i++) {
                const size_t value = (state << 1) & (size_t)G[i];
                const uint8_t parity = parity_table.parse(value);
                branch_table[i].buf[state] = parity ? soft_decision_high : soft_decision_low;
            }
        }
    }
};

#undef ALIGNED