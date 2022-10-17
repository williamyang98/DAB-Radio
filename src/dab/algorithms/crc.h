#pragma once

#include <stdint.h>

// Source: http://www.sunshine2k.de/articles/coding/crc/understanding_crc.html#ch44
// A copy of the HTML page is also stored in docs/
// A very good source on understanding how CRC works and is implemented
// The lookup implementation below is completely based on their examples
template <typename T>
class CRC_Calculator {
private:
    T lut[256] = {0};
    const T G;
    // Different CRC implementations have a non-zero initial register state
    // Additionally the CRC result may be XORed with a value prior to transmission
    T initial_value = 0u;   
    T final_xor_value = 0u;
public:
    // Generator polynomial without leading coefficient (msb left)
    CRC_Calculator(const T _G): G(_G) {
        GenerateTable();
    }
    T Process(const uint8_t* x, const int N) {
        T crc = initial_value;
        const int shift = (sizeof(T)-1)*8;

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
    void GenerateTable(void) {
        const T bitcheck = 1u << (sizeof(T)*8 - 1);
        const int shift = (sizeof(T)-1)*8;

        for (int i = 0; i < 256; i++) {
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
    }
};