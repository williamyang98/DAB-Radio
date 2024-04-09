#include "./dab_mapper_ref.h"
#include <stddef.h>
#include <vector>
#include "utility/span.h"

// DOC: ETSI EN 300 401
// Referring to clause 14.6 - Frequency interleaving
// Before the OFDM symbol is sent for packing, the order of the carriers are scrambled
// This is done so that selective fading doesn't destroy contiguous parts of the OFDM symbol bits
void get_DAB_mapper_ref(tcb::span<int> carrier_map, const size_t nb_fft) {
    const size_t N = nb_fft;
    const size_t K = N/4;
    const size_t nb_carriers = carrier_map.size();


    // Referring to clause 14.6.1
    // The equation for mode I transmissions on generating this PI table is given
    // PI_TABLE is a 1 to 1 mapping for the N-fft
    // It goes from -F <= f <= F
    // DC is positioned at index=N/2
    auto PI_TABLE = std::vector<size_t>(N);
    PI_TABLE[0] = 0;
    for (size_t i = 1; i < N; i++) {
        PI_TABLE[i] = (size_t)((13*PI_TABLE[i-1]+K-1) % N);
    }

    const size_t DC_index = N/2;
    const size_t start_index = DC_index - nb_carriers/2;
    const size_t end_index = DC_index + nb_carriers/2;

    // Referring to clause 14.6.1 - We follow the specified rule for constructing our carrier map table
    // copy all the values that are inbetween the desired carrier range
    // -F <= f <= F where f =/= 0
    // We copy these inorder from the PI_TABLE mapping
    size_t carrier_map_index = 0;
    for (size_t i = 0; i < N; i++) {
        const size_t v = PI_TABLE[i];
        // outside of valid indices
        if ((v < start_index) || (v > end_index) || (v == DC_index)) {
            continue;
        }
        // mapping for -F <= f < 0
        if (v < DC_index) {
            carrier_map[carrier_map_index++] = (int)(v-start_index);
        // mapping for 0 < f <= F
        // need to subtract one to remove DC bin
        } else {
            carrier_map[carrier_map_index++] = (int)(v-start_index-1);
        }
    }
}