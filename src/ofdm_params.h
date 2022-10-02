#pragma once

struct OFDM_Params 
{
    int nb_frame_symbols;
    int nb_symbol_period;
    int nb_null_period;
    int nb_cyclic_prefix;

    int nb_fft;
    int nb_data_carriers;

    int freq_carrier_spacing;
};