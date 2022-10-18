#include "cif_deinterleaver.h"

constexpr int TOTAL_CIF_DEINTERLEAVE = 16;

// Referring to ETSI EN 300 401
// Deinterleaving indices copied from table 21
const int CIF_INDICES_OFFSETS[TOTAL_CIF_DEINTERLEAVE] = {
    0,8,4,12, 2,10,6,14, 1,9,5,13, 3,11,7,15
};

CIF_Deinterleaver::CIF_Deinterleaver(const int _nb_bytes)
: nb_bytes(_nb_bytes) 
{
    const int nb_bits = nb_bytes*8;
    bits_buffer = new uint8_t[nb_bits*TOTAL_CIF_DEINTERLEAVE];
}

CIF_Deinterleaver::~CIF_Deinterleaver() {
    delete [] bits_buffer;
}

void CIF_Deinterleaver::Consume(const uint8_t* bytes_buf) {
    const int nb_bits = nb_bytes*8;

    // Append data into circular buffer
    auto* curr_bits_buf = &bits_buffer[nb_bits*curr_frame];
    for (int i = 0; i < nb_bytes; i++) {
        const uint8_t b = bytes_buf[i];
        for (int j = 0; j < 8; j++) {
            curr_bits_buf[8*i + j] = (b >> j) & 0b1;
        }
    }

    // Advance frame
    curr_frame = (curr_frame+1) % TOTAL_CIF_DEINTERLEAVE;
    if (total_frames_stored < TOTAL_CIF_DEINTERLEAVE) {
        total_frames_stored++;
    } 
}

bool CIF_Deinterleaver::Deinterleave(uint8_t* out_bits_buf) {
    const int nb_bits = nb_bytes*8;

    // insufficient frames to deinterleave
    if (total_frames_stored < TOTAL_CIF_DEINTERLEAVE) {
        return false;
    }

    // Create a list of buffer pointers
    // Index=0   points to the newest frame
    // Index=end points to the oldest frame
    uint8_t* BUFFER_LOOKUP[TOTAL_CIF_DEINTERLEAVE]; 
    for (int i = 0; i < TOTAL_CIF_DEINTERLEAVE; i++) {
        const int frame_index = ((curr_frame-1) -i + TOTAL_CIF_DEINTERLEAVE) % TOTAL_CIF_DEINTERLEAVE;
        BUFFER_LOOKUP[i] = &bits_buffer[frame_index*nb_bits];
    }

    // Deinterleave and store in output bits buffer
    for (int i = 0; i < nb_bits; i++) {
        const int i_mod = i % TOTAL_CIF_DEINTERLEAVE;
        // To get the bits from the same CIF before interleaving
        // We reconstruct the oldest frame since that has all of its bits stored in the buffer
        const int frame_offset = CIF_INDICES_OFFSETS[i_mod];
        const int frame_index = (TOTAL_CIF_DEINTERLEAVE-1) - frame_offset;
        out_bits_buf[i] = BUFFER_LOOKUP[frame_index][i];
    }

    return true;
}