#include <complex>
#include <stdint.h>

#include "ofdm_params.h"

typedef struct kiss_fft_state* kiss_fft_cfg;

// simulate a OFDM transmitter using one of the DAB transmission modes
// this will have a sampling rate of 2.048MHz
class OFDM_Modulator 
{
private:
    kiss_fft_cfg ifft_cfg;
    const OFDM_Params params;

    const int frame_out_size;
    const int data_in_size;

    std::complex<float>* prs_fft_ref;
    std::complex<float>* prs_time_ref;

    // frequency domain buffers used for dqpsk encoding
    std::complex<float>* last_sym_fft;
    std::complex<float>* curr_sym_fft;
public:
    OFDM_Modulator(
        const OFDM_Params _params, 
        const std::complex<float>* _prs_fft_ref);
    ~OFDM_Modulator();
    bool ProcessBlock(
        std::complex<float>* frame_out_buf, const int nb_frame_out, 
        const uint8_t* data_in_buf, const int nb_data_in);
private:
    void CreateDataSymbol(
        const uint8_t* sym_data_in, 
        std::complex<float>* sym_out);
};