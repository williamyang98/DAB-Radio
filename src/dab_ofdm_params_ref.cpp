#include "dab_ofdm_params_ref.h"

#include <stdlib.h>

OFDM_Params get_DAB_OFDM_params(const int transmission_mode) {
    OFDM_Params p; 
    switch (transmission_mode) {
    case 1:
        {
            p.nb_frame_symbols = 76;
            p.nb_symbol_period = 2552;
            p.nb_null_period = 2656;
            p.nb_fft = 2048;
            p.nb_cyclic_prefix = p.nb_symbol_period-p.nb_fft;
            p.nb_data_carriers = 1536;
            p.freq_carrier_spacing = 1000;
        }
        break;
    case 2:
        {
            p.nb_frame_symbols = 76;
            p.nb_symbol_period = 1276;
            p.nb_null_period = 1328;
            p.nb_fft = 1024;
            p.nb_cyclic_prefix = p.nb_symbol_period-p.nb_fft;
            p.nb_data_carriers = 768;
            p.freq_carrier_spacing = 2000;
        }
        break;
    case 3:
        {
            p.nb_frame_symbols = 76;
            p.nb_symbol_period = 638;
            p.nb_null_period = 664;
            p.nb_fft = 512;
            p.nb_cyclic_prefix = p.nb_symbol_period-p.nb_fft;
            p.nb_data_carriers = 384;
            p.freq_carrier_spacing = 4000;
        }
        break;
    case 4:
        break;
        {
            p.nb_frame_symbols = 153;
            p.nb_symbol_period = 319;
            p.nb_null_period = 345;
            p.nb_fft = 256;
            p.nb_cyclic_prefix = p.nb_symbol_period-p.nb_fft;
            p.nb_data_carriers = 192;
            p.freq_carrier_spacing = 8000;
        }
    default:
        // TODO: handle errors
        exit(1);
    }

    return p;
}