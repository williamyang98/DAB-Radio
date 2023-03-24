#pragma once

#include <complex>
#include "utility/span.h"

class OFDM_Demod;

void RenderSourceBuffer(tcb::span<const std::complex<float>> buf_raw);
void RenderOFDMDemodulator(OFDM_Demod& demod);