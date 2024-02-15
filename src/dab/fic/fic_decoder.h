#pragma once

#include <stdint.h>
#include <stddef.h>
#include <memory>
#include <vector>
#include "utility/observable.h"
#include "utility/span.h"
#include "viterbi_config.h"

class DAB_Viterbi_Decoder;
class AdditiveScrambler;

// Decodes the convolutionally encoded, scrambled and CRC16 group of FIGs
class FIC_Decoder 
{
private:
    std::unique_ptr<DAB_Viterbi_Decoder> m_vitdec;
    std::unique_ptr<AdditiveScrambler> m_scrambler;
    std::vector<uint8_t> m_decoded_bytes;

    const size_t m_nb_fibs_per_group;
    const size_t m_nb_encoded_bits;
    const size_t m_nb_decoded_bytes;
    const size_t m_nb_decoded_bits;

    // fib buffer
    Observable<tcb::span<const uint8_t>> obs_on_fib;
public:
    // number of bits in FIB (fast information block) group per CIF (common interleaved frame)
    FIC_Decoder(const size_t nb_encoded_bits, const size_t nb_fibs_per_group);
    ~FIC_Decoder();
    void DecodeFIBGroup(tcb::span<const viterbi_bit_t> encoded_bits, const size_t cif_index);
    auto& OnFIB(void) { return obs_on_fib; }
};