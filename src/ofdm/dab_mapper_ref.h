#pragma once

#include <stddef.h>
#include "utility/span.h"

// Generate a carrier map with a reference fft size
// carrier_map[nb_carriers]
void get_DAB_mapper_ref(tcb::span<int> carrier_map, const size_t nb_fft);