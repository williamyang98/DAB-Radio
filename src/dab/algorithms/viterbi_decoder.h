#pragma once

#include <stdint.h>

// Phil Karn's implementation uses soft decision decoding
typedef int16_t viterbi_bit_t;
struct vitdec_t;

// A wrapper around the C styled api in Phil Karn's viterbi decoder implementation
class ViterbiDecoder
{
public:
    struct DecodeResult {
        int nb_encoded_bits;
        int nb_puncture_bits;
        int nb_decoded_bits;
    };
private:
    vitdec_t* vitdec;
public:
    // _input_bits = minimum number of bits in the resulting decoded message
    ViterbiDecoder(const uint8_t _poly[4], const int _input_bits);
    ~ViterbiDecoder();
    void Reset();
    DecodeResult Update(
        const viterbi_bit_t* encoded_bits, const int nb_encoded_bits, 
        const uint8_t* puncture_code, const int nb_puncture_bits);
    void GetTraceback(uint8_t* out_bytes, const int nb_decoded_bits);
    int16_t GetPathError(const int state=0);
};