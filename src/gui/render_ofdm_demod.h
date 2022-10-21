#pragma once

#include <complex>

class OFDM_Demodulator;
class OFDM_Symbol_Mapper;

void RenderSourceBuffer(std::complex<float>* buf_raw, const int block_size);
void RenderOFDMDemodulator(OFDM_Demodulator* demod, OFDM_Symbol_Mapper* mapper);