#pragma once

#include <stdint.h>
#include <vector>
#include <memory>
#include "database/dab_database_entities.h"
#include "utility/span.h"
#include "viterbi_config.h"

class CIF_Deinterleaver;
class DAB_Viterbi_Decoder;
class AdditiveScrambler;

// Is associated with a subchannel residing inside the CIF (common interleaved frame)
// Performs deinterleaving and decoding on that subchannel
class MSC_Decoder 
{
private:
    const Subchannel subchannel;
    // Internal buffers
    const int nb_encoded_bits;
    const int nb_encoded_bytes;
    std::vector<viterbi_bit_t> encoded_bits_buf;
    std::vector<uint8_t> decoded_bytes_buf;
    // Decoders and deinterleavers
    std::unique_ptr<CIF_Deinterleaver> deinterleaver;
    std::unique_ptr<DAB_Viterbi_Decoder> vitdec;
    std::unique_ptr<AdditiveScrambler> scrambler;
public:
    MSC_Decoder(const Subchannel _subchannel);
    ~MSC_Decoder();
    // Returns the number of bytes decoded
    // NOTE: the number of bytes decoded can be 0 if the deinterleaver is still collecting frames
    tcb::span<uint8_t> DecodeCIF(tcb::span<const viterbi_bit_t> buf);
private:
    int DecodeEEP();
    int DecodeUEP();
};