#pragma once

#include <stdint.h>

class AdditiveScrambler 
{
private:
    uint16_t syncword;
    uint16_t reg;
public:
    uint8_t Process() {
        uint8_t b = 0x00;
        for (int i = 0; i < 8; i++) {
            uint8_t v = 0;
            // 1 + x^-5 + x^-9
            v ^= ((reg >> 8) & 0b1);
            v ^= ((reg >> 4) & 0b1);
            // NOTE: scrambler is operating in bit reversed mode
            b |= (v << (7-i));
            reg = (reg << 1) | v;
        }
        return b;
    }

    void SetSyncword(const uint16_t _syncword) {
        syncword = _syncword;
    }
    void Reset() {
        reg = syncword;
    }
};