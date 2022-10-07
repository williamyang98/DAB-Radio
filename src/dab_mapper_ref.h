#pragma once

#include "ofdm_params.h"

// Generate a carrier map with a reference fft size
// carrier_map[nb_carriers]
void get_DAB_mapper_ref(int* carrier_map, const int nb_carriers, const int nb_fft);