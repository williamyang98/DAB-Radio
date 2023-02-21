#pragma once

#include <stdint.h>
#include <stddef.h>
#include <assert.h>
#include "utility/aligned_vector.h"
#include "utility/parity_table.h"

// If the same parameters are used for the viterbi decoder
// We can reuse the branch table for better memory usage
template <typename soft_t>
class ViterbiBranchTable 
{
public:
    const size_t K;
    const size_t R;
    const soft_t soft_decision_high;            // soft value for high symbol
    const soft_t soft_decision_low;             // soft value for low symbol
    const size_t alignment;                     // address alignment
    const size_t stride;                    
private:
    AlignedVector<soft_t> branch_table;         // shape: (R x stride)
public:
    // NOTE: Polynomials (G) should be in binary form with least signficant bit corresponding to the input bit
    template <typename code_t>
    ViterbiBranchTable(
        const size_t constraint_length, const size_t code_rate,
        const code_t* G,                        // length = code_rate
        const soft_t _soft_decision_high,
        const soft_t _soft_decision_low,
        const size_t _alignment = 32u)
    :   K(constraint_length), 
        R(code_rate),
        soft_decision_high(_soft_decision_high),
        soft_decision_low(_soft_decision_low),
        alignment(_alignment),
        // numstates = 2^(K-1) 
        // stride = numstates/2 = 2^(K-2)
        // branch_table = (R x stride)
        stride(std::size_t(1) << (K-2u)),
        branch_table(R*stride, _alignment)
    {
        assert(K > 1u);       
        assert(R > 1u);
        assert(soft_decision_high > soft_decision_low);
        assert(branch_table.alignment() == alignment);
        // NOTE: The branch table must guarantee internal alignment of each branch
        assert((stride*sizeof(soft_t)) % alignment == 0);
        calculate_branch_table(G);
    }

    // Index like a 2D array
    inline
    soft_t* operator[](size_t index) { 
        assert(index < R);
        return &branch_table[index*stride];
    }

    inline
    const soft_t* operator[](size_t index) const { 
        assert(index < R);
        return &branch_table[index*stride];
    }

    auto begin() const { return branch_table.data(); }
    auto end() const { return branch_table.data() + size(); }
    auto data() const { return branch_table.data(); }
    auto size() const { return branch_table.size(); }
private:
    template <typename code_t>
    void calculate_branch_table(const code_t* G) {
        auto& parity_table = ParityTable::get();
        for (size_t state = 0u; state < stride; state++) {
            for (size_t i = 0u; i < R; i++) {
                const size_t value = (state << 1) & (size_t)G[i];
                const uint8_t parity = parity_table.parse(value);
                branch_table[i*stride+state] = parity ? soft_decision_high : soft_decision_low;
            }
        }
    }
};