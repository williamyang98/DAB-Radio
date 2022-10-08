#pragma once

#include <stdint.h>

// After receiving the raw dqpsk symbols, perform the following: 
// 1. Constellation mapping
//      H1: {-3*pi/4, -pi/4, pi/4, 3*pi/4} => {3,1,0,2}
//      modulator: {0,1,2,3}
//      lut_table[4] = {3,1,0,2}
// 2. Frequency deinterleaving
//      Bits in transmission frame are distributed according to a rule
//      This way burst errors once deinterleaved is distributed
//      This increases the effectiveness of the ECC 

class OFDM_Symbol_Mapper 
{
private:
    // frame size
    const int nb_symbols;
    const int nb_carriers;
    // output data buffer
    uint8_t* out_buf;
    const int out_buf_length;
    const int nb_symbol_byte_length; // to stride between symbols in same frame
    // map symbols and data carriers
    uint8_t* symbol_mapper;
    int* carrier_mapper;
    // perform the bit interleaving
    uint8_t* bits_interleaved;
    uint8_t* bits_deinterleaved;
public:
    OFDM_Symbol_Mapper(const int* _carrier_mapper, const int _nb_carriers, const int _nb_symbols, const uint8_t* _symbol_mapper=NULL);
    ~OFDM_Symbol_Mapper();
    void ProcessRawFrame(const uint8_t* phases);
    inline uint8_t* GetOutputBuffer(void) { return out_buf; }
    inline int GetOutputBufferSize(void) const { return out_buf_length; }
    inline int GetOutputBufferSymbolStride(void) const { return nb_symbol_byte_length; }
    inline int GetTotalSymbols(void) const { return nb_symbols; }
    inline int GetTotalCarriers(void) const { return nb_carriers; }
private:
    void ProcessSymbol(const uint8_t* phases, uint8_t* sym_out_buf);
};