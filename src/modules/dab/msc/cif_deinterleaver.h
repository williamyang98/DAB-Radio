#pragma once

#include "viterbi_config.h"

// Used to deinterleave DAB logical frames coming over a subchannel
// Refer to ETSI EN 300 401 Clause 12 for a detailed explanation
class CIF_Deinterleaver 
{
private:
    viterbi_bit_t* bits_buffer;
    const int nb_bytes;
    int curr_frame = 0;
    int total_frames_stored = 0;
public:
    CIF_Deinterleaver(const int _nb_bytes);
    ~CIF_Deinterleaver();
    // Consume a buffer of nb_bytes and store 
    void Consume(const viterbi_bit_t* bits_buf); 
    // Output the deinterleaved bits into a bits array
    bool Deinterleave(viterbi_bit_t* out_bits_buf);
};