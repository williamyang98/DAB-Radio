#pragma once

#include <stdint.h>
#include <memory>
#include <vector>
#include "utility/observable.h"
#include "utility/span.h"
#include "viterbi_config.h"

class ViterbiDecoder;
class AdditiveScrambler;

// Decodes the convolutionally encoded, scrambled and CRC16 group of FIGs
class FIC_Decoder 
{
private:
    std::unique_ptr<ViterbiDecoder> vitdec;
    std::unique_ptr<AdditiveScrambler> scrambler;
    std::vector<uint8_t> decoded_bytes;

    const int nb_fibs_per_group;
    const int nb_encoded_bits;
    const int nb_decoded_bytes;
    const int nb_decoded_bits;

    // fib buffer
    Observable<tcb::span<const uint8_t>> obs_on_fib;
public:
    // number of bits in FIB (fast information block) group per CIF (common interleaved frame)
    FIC_Decoder(const int _nb_encoded_bits, const int _nb_fibs_per_group);
    ~FIC_Decoder();
    void DecodeFIBGroup(tcb::span<const viterbi_bit_t> encoded_bits, const int cif_index);
    auto& OnFIB(void) { return obs_on_fib; }
};