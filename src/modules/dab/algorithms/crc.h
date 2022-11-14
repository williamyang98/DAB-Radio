#pragma once

#include <stdint.h>
#include <unordered_map>
#include "utility/span.h"

// Source: http://www.sunshine2k.de/articles/coding/crc/understanding_crc.html#ch44
// A copy of the HTML page is also stored in docs/
// A very good source on understanding how CRC works and is implemented
// The lookup implementation below is completely based on their examples
template <typename T>
class CRC_Calculator {
private:
    // Global lut for CRC lookup tables for all CRC polynomials
    // key = polynomial, value = lookup table
    // NOTE: inline keyword allows for initialization of static template member
    //       without it we get a segmentation fault when using the table (c++17 required)
    // TODO: this runtime initialisation doesn't seem to work for non MSVC compilers
    //       on these other platforms we get a math error because it does (x % nb_bins) when nb_bins=0
    inline static std::unordered_map<T, T*> LOOKUP_TABLES;
private:
    T* lut;
    const T G;
    // Different CRC implementations have a non-zero initial register state
    // Additionally the CRC result may be XORed with a value prior to transmission
    T initial_value = 0u;   
    T final_xor_value = 0u;
public:
    // Generator polynomial without leading coefficient (msb left)
    CRC_Calculator(const T _G): G(_G) {
        lut = CRC_Calculator<T>::GenerateTable(G);
    }
    T Process(tcb::span<const uint8_t> x) {
        T crc = initial_value;
        const size_t shift = (sizeof(T)-1)*8;
        const size_t N = x.size();
        for (int i = 0; i < N; i++) {
            crc = crc ^ ((T)(x[i]) << shift);
            uint8_t lut_idx = (crc >> shift) & 0xFF;
            crc = (crc << 8) ^ lut[lut_idx];
        }
        return crc ^ final_xor_value;
    }
    inline void SetInitialValue(const T x) { initial_value = x; }
    inline void SetFinalXORValue(const T x) { final_xor_value = x; }
private:
    static T* GenerateTable(const T G) {
        auto& table = CRC_Calculator<T>::LOOKUP_TABLES;
        auto res = table.find(G);
        if (res != table.end()) {
            return res->second;
        }

        const T bitcheck = 1u << (sizeof(T)*8 - 1);
        const int shift = (sizeof(T)-1)*8;

        const int N = 256;
        auto* lut = new T[N];
        for (int i = 0; i < N; i++) {
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