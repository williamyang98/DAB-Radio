#pragma once
#include <stdint.h>

/* These are the function definitions for Phil Karn's 2002 Reed Solomon decoder
 * A mirror of his code is found here: https://github.com/zleffke/libfec
 * His website (which doesn't seem to work properly) is found here: http://www.ka9q.net/code/fec/
 * Copyright 2002-2004 Phil Karn, KA9Q
 * May be used under the terms of the GNU Lesser General Public License (LGPL)
 */
struct RS_data;

// This is just a thin wrapper around Phil Karn's code to manage memory
class Reed_Solomon_Decoder 
{
private:
    struct RS_data* m_rs;
public:
    Reed_Solomon_Decoder(
        const int symbol_size, const int galois_field_polynomial,
        const int fcr, const int primer, const int nb_roots, const int pad);
    ~Reed_Solomon_Decoder();
    Reed_Solomon_Decoder(Reed_Solomon_Decoder&) = delete;
    Reed_Solomon_Decoder(Reed_Solomon_Decoder&&) = delete;
    Reed_Solomon_Decoder& operator=(Reed_Solomon_Decoder&) = delete;
    Reed_Solomon_Decoder& operator=(Reed_Solomon_Decoder&&) = delete;
    int Decode(uint8_t* data, int* eras_pos, int no_eras);
};


