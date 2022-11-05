#include "ofdm_modulator.h"
#include <kiss_fft.h>

OFDM_Modulator::OFDM_Modulator(
    const OFDM_Params _params, 
    const std::complex<float>* _prs_fft_ref)
:   params(_params),
    frame_out_size(_params.nb_null_period + _params.nb_symbol_period*_params.nb_frame_symbols),
    data_in_size((_params.nb_frame_symbols-1)*_params.nb_data_carriers*2/8)
{
    ifft_cfg = kiss_fft_alloc(params.nb_fft, true, NULL, NULL);

    // interleave the bits for a OFDM symbol containing N data carriers
    prs_fft_ref = new std::complex<float>[params.nb_fft];
    prs_time_ref = new std::complex<float>[params.nb_symbol_period];

    {
        for (int i = 0; i < params.nb_fft; i++) {
            prs_fft_ref[i] = _prs_fft_ref[i];
        }
    }

    // create our time domain prs symbol with the cyclic prefix
    {
        auto* buf = &prs_time_ref[params.nb_cyclic_prefix];
        kiss_fft(ifft_cfg, (kiss_fft_cpx*)prs_fft_ref, (kiss_fft_cpx*)buf);
        for (int i = 0; i < params.nb_cyclic_prefix; i++) {
            prs_time_ref[i] = prs_time_ref[params.nb_fft+i];
        }
    }

    last_sym_fft = new std::complex<float>[params.nb_fft];
    curr_sym_fft = new std::complex<float>[params.nb_fft];

    for (int i = 0; i < params.nb_fft; i++) {
        curr_sym_fft[i] = std::complex<float>(0,0);
        last_sym_fft[i] = std::complex<float>(0,0);
    }
}

OFDM_Modulator::~OFDM_Modulator() 
{
    kiss_fft_free(ifft_cfg);
    delete [] prs_fft_ref;
    delete [] prs_time_ref;

    delete [] last_sym_fft;
    delete [] curr_sym_fft;
}

bool OFDM_Modulator::ProcessBlock(
    std::complex<float>* frame_out_buf, const int nb_frame_out, 
    const uint8_t* data_in_buf, const int nb_data_in)
{
    // invalid buffer sizes
    if (nb_data_in != data_in_size) {
        return false;
    }
    if (nb_frame_out != frame_out_size) {
        return false;
    }

    // null period
    for (int i = 0; i < params.nb_null_period; i++) {
        frame_out_buf[i] = std::complex<float>(0,0);
    }

    // prs symbol
    for (int i = 0; i < params.nb_symbol_period; i++) {
        frame_out_buf[i+params.nb_null_period] = prs_time_ref[i];
    }

    // seed the dqpsk fft buffers
    for (int i = 0; i < params.nb_fft; i++) {
        last_sym_fft[i] = prs_fft_ref[i];
    }

    // data symbols
    {
        const int nb_data_symbols = params.nb_frame_symbols-1;
        const int nb_data_in = params.nb_data_carriers*2/8; 
        const int nb_out = params.nb_symbol_period;
        for (int i = 0; i < nb_data_symbols; i++) {
            const auto* sym_data_in = &data_in_buf[i*nb_data_in];
            // account for the PRS (phase reference symbol)
            auto* sym_out = &frame_out_buf[params.nb_null_period + nb_out*(i+1)];
            CreateDataSymbol(sym_data_in, sym_out);
        }
    }

    return true;
}

void OFDM_Modulator::CreateDataSymbol(const uint8_t* sym_data_in, std::complex<float>* sym_out)
{
    const int nb_data_in = params.nb_data_carriers*2/8; 
    const int nb_out = params.nb_symbol_period;

    static float A = 1.0f/std::sqrt(2.0f);
    static std::complex<float> PHASE_MAP[4] = {
        {-A,-A}, {A,-A}, {A,A}, {-A,A}};

    // Create raw fft bins
    {
        // create fft for -F/2 <= f < 0
        int curr_carrier = params.nb_fft - params.nb_data_carriers/2;
        for (int i = 0; i < nb_data_in/2; i++) {
            const uint8_t b = sym_data_in[i];
            curr_sym_fft[curr_carrier++] = PHASE_MAP[((b >> 0) & 0b11)];
            curr_sym_fft[curr_carrier++] = PHASE_MAP[((b >> 2) & 0b11)];
            curr_sym_fft[curr_carrier++] = PHASE_MAP[((b >> 4) & 0b11)];
            curr_sym_fft[curr_carrier++] = PHASE_MAP[((b >> 6) & 0b11)];
        }

        // create fft for 0 < f <= F/2 
        curr_carrier = 1;
        for (int i = 0; i < nb_data_in/2; i++) {
            const int j = nb_data_in/2 + i;
            const uint8_t b = sym_data_in[j];
            curr_sym_fft[curr_carrier++] = PHASE_MAP[((b >> 0) & 0b11)];
            curr_sym_fft[curr_carrier++] = PHASE_MAP[((b >> 2) & 0b11)];
            curr_sym_fft[curr_carrier++] = PHASE_MAP[((b >> 4) & 0b11)];
            curr_sym_fft[curr_carrier++] = PHASE_MAP[((b >> 6) & 0b11)];
        }
    }


    // get the dqpsk
    // arg(z0*z1) = arg(z0) + arg(z1)
    {
        for (int i = 0; i < params.nb_data_carriers/2; i++) {
            const int j = params.nb_fft - params.nb_data_carriers/2 + i;
            curr_sym_fft[j] = last_sym_fft[j] * curr_sym_fft[j];
        }

        for (int i = 0; i < params.nb_data_carriers/2; i++) {
            const int j = 1+i;
            curr_sym_fft[j] = last_sym_fft[j] * curr_sym_fft[j];
        }
    }

    // get ifft of symbol
    {
        auto* buf = &sym_out[params.nb_cyclic_prefix];
        kiss_fft(ifft_cfg, (kiss_fft_cpx*)curr_sym_fft, (kiss_fft_cpx*)buf);
    }

    // create cyclic prefix
    for (int i = 0; i < params.nb_cyclic_prefix; i++) {
        sym_out[i] = sym_out[i+params.nb_fft];
    }

    // swap fft buffers
    {
        auto* tmp = last_sym_fft;
        last_sym_fft = curr_sym_fft;
        curr_sym_fft = tmp;
    }
}