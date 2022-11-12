#pragma once

#include <complex>

class OFDM_Demod;

void RenderSourceBuffer(const std::complex<float>* buf_raw, const int block_size);
void RenderOFDMDemodulator(OFDM_Demod* demod);