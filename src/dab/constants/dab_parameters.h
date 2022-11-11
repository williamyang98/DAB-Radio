#pragma once

#include <stdexcept>

struct DAB_Parameters {
    int nb_frame_bits;      // number of bits in frame
    int nb_symbols;         // number of symbols in frame
    int nb_fic_symbols;     // fast information channel symbols
    int nb_msc_symbols;     // main service channel symbols
    int nb_fibs;            // number of fast information blocks in fast information channel
    int nb_cifs;            // number of common interleaved frames in main service channel
    int nb_fibs_per_cif;    // number of fast information blocks per common interleaved frame

    // additional constants from above parameters
    int nb_sym_bits;
    int nb_fic_bits;
    int nb_msc_bits;
    int nb_fib_bits;
    int nb_fib_cif_bits;    // A group of FIBs are combined and decoded for each CIF
    int nb_cif_bits;
};

// DOC: docs/DAB_parameters.pdf
// Clause A1.1 - System parameters
// Clause A1.3 - Coarse structure of the transmission frame
static DAB_Parameters get_dab_parameters(const int transmission_mode) 
{
    DAB_Parameters p;
    switch (transmission_mode) 
    {
    case 1:
        {
            p.nb_frame_bits = 1536*2*(76-1);
            p.nb_symbols = 76-1;
            p.nb_fic_symbols = 3;
            p.nb_msc_symbols = 72;
            p.nb_fibs = 12;
            p.nb_cifs = 4;
            p.nb_fibs_per_cif = 3;
        }
        break;
    case 2:
        {
            p.nb_frame_bits = 384*2*(76-1);
            p.nb_symbols = 76-1;
            p.nb_fic_symbols = 3;
            p.nb_msc_symbols = 72;
            p.nb_fibs = 3;
            p.nb_cifs = 1;
            p.nb_fibs_per_cif = 3;
        }
        break;
    case 3:
        {
            p.nb_frame_bits = 192*2*(153-1);
            p.nb_symbols = 153-1;
            p.nb_fic_symbols = 8;
            p.nb_msc_symbols = 144;
            p.nb_fibs = 4;
            p.nb_cifs = 1;
            p.nb_fibs_per_cif = 4;
        }
        break;
    case 4:
        {
            p.nb_frame_bits = 768*2*(76-1);
            p.nb_symbols = 76-1;
            p.nb_fic_symbols = 3;
            p.nb_msc_symbols = 72;
            p.nb_fibs = 6;
            p.nb_cifs = 2;
            p.nb_fibs_per_cif = 3;
        }
        break;
    default:
        throw std::runtime_error("Invalid transmission mode");
        break;
    }

    p.nb_sym_bits = p.nb_frame_bits / p.nb_symbols;
    p.nb_fic_bits = p.nb_sym_bits * p.nb_fic_symbols;
    p.nb_msc_bits = p.nb_sym_bits * p.nb_msc_symbols;

    p.nb_fib_bits = p.nb_fic_bits / p.nb_fibs;
    p.nb_fib_cif_bits = p.nb_fib_bits * p.nb_fibs_per_cif;

    p.nb_cif_bits = p.nb_msc_bits / p.nb_cifs;

    return p;
}