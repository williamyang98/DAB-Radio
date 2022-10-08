#include "dab_mapper_ref.h"
#include <assert.h>

void get_DAB_mapper_ref(int* carrier_map, const int nb_carriers, const int nb_fft) {
    const int N = nb_fft;
    const int K = N/4;

    // PI_TABLE is a 1 to 1 mapping for the N-fft
    // It goes from -F <= f <= F
    // DC is positioned at index=N/2
    int* PI_TABLE = new int[N];
    PI_TABLE[0] = 0;
    for (int i = 1; i < N; i++) {
        PI_TABLE[i] = (13*PI_TABLE[i-1]+K-1) % N;
    }

    const int DC_index = N/2;
    const int start_index = DC_index - nb_carriers/2;
    const int end_index = DC_index + nb_carriers/2;
    // copy all the values that are inbetween the desired carrier range
    // -F <= f <= F where f =/= 0
    // We copy these inorder from the PI_TABLE mapping
    int carrier_map_index = 0;
    for (int i = 0; i < N; i++) {
        const int v = PI_TABLE[i];
        // outside of valid indices
        if ((v < start_index) || (v > end_index) || (v == DC_index)) {
            continue;
        }
        // mapping for -F <= f < 0
        if (v < DC_index) {
            carrier_map[carrier_map_index++] = v - start_index;
        // mapping for 0 < f <= F
        // need to subtract one to remove DC bin
        } else {
            carrier_map[carrier_map_index++] = v - start_index - 1;
        }
    }
    assert(carrier_map_index == nb_carriers);
    delete [] PI_TABLE;
}