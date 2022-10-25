#pragma once
#include <stdint.h>

// Used to deinterleave DAB logical frames coming over a subchannel
// Refer to ETSI EN 300 401 Clause 12 for a detailed explanation
// NOTE: We are using int16_t since the viterbi decoder is using soft decision boundaries
typedef int16_t deinterleaver_bit_t;

class CIF_Deinterleaver 
{
private:
    int16_t* bits_buffer;
    const int nb_bytes;
    int curr_frame = 0;
    int total_frames_stored = 0;
public:
    CIF_Deinterleaver(const int _nb_bytes);
    ~CIF_Deinterleaver();
    // Consume a buffer of nb_bytes and store 
    void Consume(const uint8_t* bytes_buf); 
    // Output the deinterleaved bits into a bits array
    bool Deinterleave(deinterleaver_bit_t* out_bits_buf);
};