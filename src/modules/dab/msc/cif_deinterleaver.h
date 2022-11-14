#pragma once

#include <vector>
#include "utility/span.h"
#include "viterbi_config.h"

// Used to deinterleave DAB logical frames coming over a subchannel
// Refer to ETSI EN 300 401 Clause 12 for a detailed explanation
class CIF_Deinterleaver 
{
private:
    std::vector<viterbi_bit_t> bits_buffer;
    const int nb_bytes;
    int curr_frame = 0;
    int total_frames_stored = 0;
public:
    CIF_Deinterleaver(const int _nb_bytes);
    // Consume a buffer of nb_bytes and store 
    void Consume(tcb::span<const viterbi_bit_t> bits_buf); 
    // Output the deinterleaved bits into a bits array
    bool Deinterleave(tcb::span<viterbi_bit_t> out_bits_buf);
};