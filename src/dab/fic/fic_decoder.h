#pragma once

#include <stdint.h>
#include <functional>

class Trellis;
class ViterbiDecoder;
class AdditiveScrambler;
template <typename T>
class CRC_Calculator;

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
    CRC_Calculator<uint16_t>* crc16_calc;
    Callback* callback = NULL;
    uint8_t* encoded_bits;
    uint8_t* decoded_bits;
    uint8_t* decoded_bytes;

    const int nb_encoded_bytes;
    const int nb_encoded_bits;
    const int nb_decoded_bytes;
    const int nb_decoded_bits;
public:
    FIC_Decoder();
    ~FIC_Decoder();
    void DecodeFIBGroup(const uint8_t* encoded_bytes, const int cif_index);
    void SetCallback(Callback* _callback) { callback = _callback; }
};