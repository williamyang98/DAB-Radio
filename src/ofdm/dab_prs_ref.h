#pragma once
#include <complex>

// Populate the buffer with the fft of the PRS signal in time domain
// This is used to correlated with the received PRS to get the sample offset for
// fine time frame synchronisation
// Correlation is done by multiplication in the frequency domain,
// then the inverse fft is done to get the impulse response
void get_DAB_PRS_reference(const int transmission_mode,
    std::complex<float>* buf,
    const int nb_fft);