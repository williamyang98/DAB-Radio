#include "./cif_deinterleaver.h"
#include "utility/span.h"
#include "viterbi_config.h"

// DOC: ETSI EN 300 401
// Clause 12 - Time interleaving
// Deinterleaving indices copied from table 21
constexpr int TOTAL_CIF_DEINTERLEAVE = 16;
const int CIF_INDICES_OFFSETS[TOTAL_CIF_DEINTERLEAVE] = {
    0,8,4,12, 2,10,6,14, 1,9,5,13, 3,11,7,15
};

CIF_Deinterleaver::CIF_Deinterleaver(const int nb_bytes)
: m_nb_bytes(nb_bytes) 
{
    const int nb_bits = m_nb_bytes*8;
    m_bits_buffer.resize(nb_bits*TOTAL_CIF_DEINTERLEAVE);
}

void CIF_Deinterleaver::Consume(tcb::span<const viterbi_bit_t> bits_buf) {
    const int nb_bits = m_nb_bytes*8;

    // Append data into circular buffer
    auto* curr_bits_buf = &m_bits_buffer[nb_bits*m_curr_frame];
    for (int i = 0; i < nb_bits; i++) {
        curr_bits_buf[i] = bits_buf[i];
    }

    // Advance frame
    m_curr_frame = (m_curr_frame+1) % TOTAL_CIF_DEINTERLEAVE;
    if (m_total_frames_stored < TOTAL_CIF_DEINTERLEAVE) {
        m_total_frames_stored++;
    } 
}

bool CIF_Deinterleaver::Deinterleave(tcb::span<viterbi_bit_t> out_bits_buf) {
    const int nb_bits = m_nb_bytes*8;

    // insufficient frames to deinterleave
    if (m_total_frames_stored < TOTAL_CIF_DEINTERLEAVE) {
        return false;
    }

    // Create a list of buffer pointers
    // Index=0   points to the newest frame
    // Index=end points to the oldest frame
    viterbi_bit_t* BUFFER_LOOKUP[TOTAL_CIF_DEINTERLEAVE]; 
    for (int i = 0; i < TOTAL_CIF_DEINTERLEAVE; i++) {
        const int frame_index = ((m_curr_frame-1) -i + TOTAL_CIF_DEINTERLEAVE) % TOTAL_CIF_DEINTERLEAVE;
        BUFFER_LOOKUP[i] = &m_bits_buffer[frame_index*nb_bits];
    }

    // DOC: ETSI EN 300 401
    // Clause 12 - Time interleaving
    // Referring to this section, we can reconstruct a frame 
    // from all the stored frames in our circular buffer
    // TODO: The specification also states that on a multiplex reconfiguration occurs the deinterleaving changes
    //       Implement a way to handle this

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