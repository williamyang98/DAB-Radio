#pragma once
#include <stdint.h>

/* These are the function definitions for Phil Karn's 2002 Reed Solomon decoder
 * A mirror of his code is found here: https://github.com/zleffke/libfec
 * His website (which doesn't seem to work properly) is found here: http://www.ka9q.net/code/fec/
 * Copyright 2002-2004 Phil Karn, KA9Q
 * May be used under the terms of the GNU Lesser General Public License (LGPL)
 */
struct RS_data;
struct RS_data *init_rs_char(int symsize, int gfpoly, int fcr, int prim, int nroots, int pad);
void free_rs_char(struct RS_data *rs);
int decode_rs_char(struct RS_data* rs, uint8_t *data, int *eras_pos, int no_eras);

// This is just a thin wrapper around Phil Karn's code to manage memory
class Reed_Solomon_Decoder 
{
private:
    struct RS_data* rs;
public:
    Reed_Solomon_Decoder(
        const int symbol_size, const int galois_field_polynomial,
        const int fcr, const int primer, const int nb_roots, const int pad)
    {
        rs = init_rs_char(
            symbol_size, galois_field_polynomial,
            fcr, primer, nb_roots, pad);
    }

    ~Reed_Solomon_Decoder() {
        free_rs_char(rs);
    }

    int Decode(uint8_t* data, int* eras_pos, int no_eras) {
        return decode_rs_char(rs, data, eras_pos, no_eras);
    }
};


