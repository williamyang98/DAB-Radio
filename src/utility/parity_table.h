#pragma once

#include <stdint.h>

// Check if there are an even or odd number of 1s
// Odd = 1, Even = 0
class ParityTable 
{
private:
    uint8_t* table;
    ParityTable() {
        constexpr size_t N = 256;
        table = new uint8_t[N];
        for (size_t i = 0u; i < N; i++) {
            uint8_t parity = 0u;
            uint8_t b = static_cast<uint8_t>(i);
            for (size_t j = 0u; j < 8u; j++) {
                parity ^= (b & 0b1);
                b = b >> 1u;
            }
            table[i] = parity;
        }
    }
    ParityTable(const ParityTable&) = default;
    ParityTable(ParityTable&&) = default;
    ParityTable& operator=(const ParityTable&) = default;
    ParityTable& operator=(ParityTable&&) = default;
public:
    static 
    ParityTable& get() {
        static auto parity_table = ParityTable();
        return parity_table;
    }

    uint8_t parse(uint8_t x) {
        return table[x];
    }

    template <typename T>
    uint8_t parse(T x) {
        constexpr size_t N = sizeof(x);
        size_t TOTAL_BITS = N*8;
        while (TOTAL_BITS > 8u) {
            TOTAL_BITS = TOTAL_BITS >> 1;
            x = x ^ (x >> TOTAL_BITS);
        }
        return table[uint8_t(x & T(0xFF))];
    }
};