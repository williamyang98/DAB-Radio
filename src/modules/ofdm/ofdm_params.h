#pragma once

#include <stdint.h>

struct OFDM_Params 
{
    size_t nb_frame_symbols;
    size_t nb_symbol_period;
    size_t nb_null_period;
    size_t nb_cyclic_prefix;

    size_t nb_fft;
    size_t nb_data_carriers;

    size_t freq_carrier_spacing;
};