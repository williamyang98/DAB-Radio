#define _USE_MATH_DEFINES
#include <cmath>

#include <assert.h>

#include "ofdm_symbol_mapper.h"

// static const uint8_t QPSK_GRAY_CODE[4] = {3,1,0,2};
static const uint8_t QPSK_GRAY_CODE[4] = {3,2,0,1};

OFDM_Symbol_Mapper::OFDM_Symbol_Mapper(
    const int* _carrier_mapper, const int _nb_carriers, 
    const int _nb_symbols, 
    const uint8_t* _symbol_mapper)
:   nb_symbols(_nb_symbols), nb_carriers(_nb_carriers),
    nb_symbol_byte_length(_nb_carriers*2/8),
    out_buf_length(_nb_symbols*_nb_carriers*2/8)
{
    // number of carriers should be a power of 2
    assert((nb_carriers % 4) == 0);

    out_buf = new uint8_t[out_buf_length];
    symbol_mapper = new uint8_t[4];
    carrier_mapper = new int[nb_carriers];

    // we map the qpsk constellation to the gray code
    const uint8_t* actual_symbol_mapper = _symbol_mapper ? _symbol_mapper : QPSK_GRAY_CODE;
    for (int i = 0; i < 4; i++) {
        symbol_mapper[i] = actual_symbol_mapper[i];
    }

    // we do frequency interleaving 
    for (int i = 0; i < nb_carriers; i++) {
        carrier_mapper[i] = _carrier_mapper[i];
    }

    bits_interleaved = new uint8_t[2*nb_carriers];
    bits_deinterleaved = new uint8_t[2*nb_carriers];
}

OFDM_Symbol_Mapper::~OFDM_Symbol_Mapper()
{
    delete [] out_buf;
    delete [] symbol_mapper;
    delete [] carrier_mapper;
    delete [] bits_interleaved;
    delete [] bits_deinterleaved;
}

void OFDM_Symbol_Mapper::ProcessRawFrame(const uint8_t* phases)
{
    for (int i = 0; i < nb_symbols; i++) {
        auto sym_pred = &phases[i*nb_carriers];
        auto sym_out_buf = &out_buf[i*nb_symbol_byte_length];
        ProcessSymbol(sym_pred, sym_out_buf);
    }
}

void OFDM_Symbol_Mapper::ProcessSymbol(const uint8_t* phases, uint8_t* sym_out_buf)
{
    // perform carrier mapping and bit extraction
    for (int i = 0; i < nb_carriers; i++) {
        const uint8_t delta_phase = phases[carrier_mapper[i]];
        const uint8_t bits = symbol_mapper[delta_phase];
        bits_interleaved[2*i + 0] = (bits & 0b01);
        bits_interleaved[2*i + 1] = (bits & 0b10) >> 1;
    }

    // deinterleave the bits
    for (int i = 0; i < nb_carriers; i++) {
        bits_deinterleaved[i] = bits_interleaved[2*i];
        bits_deinterleaved[nb_carriers+i] = bits_interleaved[2*i + 1];
    }

    // pack the bits into data buffer
    int bit_index = 0;
    for (int i = 0; i < nb_symbol_byte_length; i++) {
        uint8_t b = 0x00;
        for (int j = 0; j < 8; j++) {
            b |= (bits_deinterleaved[bit_index++] << j);
        }
        sym_out_buf[i] = b;
    }
}