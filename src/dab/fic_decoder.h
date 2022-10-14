#pragma once

#include <stdint.h>
#include "CRC.h"
#include <functional>

class Trellis;
class ViterbiDecoder;
class AdditiveScrambler;

// Decodes the convolutionally encoded, scrambled and CRC16 group of FIGs
class FIC_Decoder 
{
public:
    struct Callback {
        virtual void OnDecodeFIBGroup(const uint8_t* buf, const int N, const int cif_index) = 0;
    };
private:
    Trellis* trellis;
    ViterbiDecoder* vitdec;
    AdditiveScrambler* scrambler;
    const uint16_t crc16_poly;
    CRC::Table<crcpp_uint16, 16>* crc16_table;
    Callback* callback = NULL;
public:
    FIC_Decoder();
    ~FIC_Decoder();
    void DecodeFIBGroup(const uint8_t* encoded_bytes, const int cif_index);
    void SetCallback(Callback* _callback) { callback = _callback; }
};