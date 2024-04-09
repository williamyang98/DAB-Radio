#pragma once

#include <stdint.h>
#include <unordered_map> // NOLINT
#include "utility/span.h"

// Source: http://www.sunshine2k.de/articles/coding/crc/understanding_crc.html#ch44
// A copy of the HTML page is also stored in docs/
// A very good source on understanding how CRC works and is implemented
// The lookup implementation below is completely based on their examples
template <typename T>
class CRC_Calculator {
private:
    T* m_lut;
    const T m_G;
    // Different CRC implementations have a non-zero initial register state
    // Additionally the CRC result may be XORed with a value prior to transmission
    T m_initial_value = 0u;   
    T m_final_xor_value = 0u;
public:
    // Generator polynomial without leading coefficient (msb left)
    explicit CRC_Calculator(const T G): m_G(G) {
        m_lut = CRC_Calculator<T>::GenerateTable(m_G);
    }
    T Process(tcb::span<const uint8_t> x) {
        T crc = m_initial_value;
        const size_t shift = (sizeof(T)-1)*8;
        const size_t N = x.size();
        for (size_t i = 0; i < N; i++) {
            crc = crc ^ ((T)(x[i]) << shift);
            uint8_t lut_idx = (crc >> shift) & 0xFF;
            crc = (crc << 8) ^ m_lut[lut_idx];
        }
        return crc ^ m_final_xor_value;
    }
    inline void SetInitialValue(const T x) { m_initial_value = x; }
    inline void SetFinalXORValue(const T x) { m_final_xor_value = x; }
private:
    static T* GenerateTable(const T G) {
        // Global lut for CRC lookup tables for all CRC polynomials
        // key = polynomial, value = lookup table
        static auto table = std::unordered_map<T, T*>{};
        auto res = table.find(G);
        if (res != table.end()) {
            return res->second;
        }

        const T bitcheck = 1u << (sizeof(T)*8 - 1);
        const int shift = (sizeof(T)-1)*8;

        const size_t N = 256;
        auto* lut = new T[N];
        for (size_t i = 0; i < N; i++) {
            T crc = static_cast<uint8_t>(i) << shift;
            for (int j = 0; j < 8; j++) {
                if ((crc & bitcheck) != 0) {
                    crc = crc << 1;
                    crc = crc ^ G;
                } else {
                    crc = crc << 1;
                }
            }
            lut[i] = crc;
        }

        table.insert({G, lut});
        return lut;
    }
};