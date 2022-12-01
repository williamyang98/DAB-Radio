#pragma once

#include <stdint.h>
#include <vector>
#include "utility/span.h"
#include "viterbi_config.h"

// Phil Karn's implementation
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
    // We depuncture our encoded bits in blocks of max_depunctured_bits
    // COMPUTETYPE=int16_t
    std::vector<int16_t> depunctured_bits;
    const int max_decoded_bits;
    const int max_depunctured_bits;
public:
    // _input_bits = minimum number of bits in the resulting decoded message
    ViterbiDecoder(const uint8_t _poly[4], const int _input_bits, const int _max_decoded_bits=1024);
    ~ViterbiDecoder();
    ViterbiDecoder(ViterbiDecoder&) = delete;
    ViterbiDecoder(ViterbiDecoder&&) = delete;
    ViterbiDecoder& operator=(ViterbiDecoder&) = delete;
    ViterbiDecoder& operator=(ViterbiDecoder&&) = delete;
    void Reset();
    DecodeResult Update(
        tcb::span<const viterbi_bit_t> encoded_bits,
        tcb::span<const uint8_t> puncture_code,
        const int nb_puncture_bits);
    void GetTraceback(tcb::span<uint8_t> out_bytes);
    int16_t GetPathError(const int state=0);
};