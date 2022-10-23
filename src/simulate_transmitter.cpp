// This application produces a dummy OFDM signal with mode I,II,III,IV parameters
// No information is encoded in this signal
// It is only used to test if the OFDM demodulator is working correctly

#define _USE_MATH_DEFINES
#include <cmath>
#include <complex>
#include <stdio.h>
#include <kiss_fft.h>

#include "ofdm_modulator.h"
#include "ofdm_params.h"
#include "dab_prs_ref.h"
#include "dab_ofdm_params_ref.h"
#include "dab_mapper_ref.h"

#include "./getopt/getopt.h"

#include <io.h>
#include <fcntl.h>

// scrambler that is used for DVB transmissions
class Scrambler 
{
private:
    uint16_t reg = 0;
public:
    uint16_t syncword = 0b0000000010101001;
    void Reset() {
        reg = syncword;
    }
    uint8_t Process() {
        uint8_t v = static_cast<uint8_t>(
            ((reg ^ (reg << 1)) >> 8) & 0xFF);
        reg = (reg << 8) | v;
        return v;
    }
};

void ApplyFrequencyShift(
    const std::complex<float>* x, std::complex<float>* y, const int N, 
    const float frequency, const float Ts=1.0f/2.048e6)
{
    float dt = 0.0f;
    for (int i = 0; i < N; i++) {
        auto pll = std::complex<float>(
            std::cosf(dt),
            std::sinf(dt));
        y[i] = x[i] * pll;
        dt += 2.0f * (float)M_PI * frequency * Ts;
    }
}

void usage() {
    fprintf(stderr, 
        "simulate_transmitter, produces OFDM data as raw IQ values\n\n"
        "\t[-M dab transmission mode (default: 1)]\n"
        "\t[-P (output the binary data used as placeholder)\n"
        "\t[-h (show usage)]\n"
    );
}

int main(int argc, char** argv) 
{
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);

    int transmission_mode = 1;
    bool print_sample_message = false;

    int opt; 
    while ((opt = getopt(argc, argv, "M:Ph")) != -1) {
        switch (opt) {
        case 'M':
            transmission_mode = (int)(atof(optarg));
            if (transmission_mode <= 0 || transmission_mode > 4) {
                fprintf(stderr, "Transmission modes: I,II,III,IV are supported not (%d)\n", transmission_mode);
                return 1;
            }
            break;
        case 'P':
            print_sample_message = true; 
            break;
        case 'h':
        default:
            usage();
            return 0;
        }
    }

    const auto params = get_DAB_OFDM_params(transmission_mode);

    auto prs_fft_ref = new std::complex<float>[params.nb_fft];
    auto carrier_mapper = new int[params.nb_data_carriers];
    
    get_DAB_PRS_reference(transmission_mode, prs_fft_ref, params.nb_fft);
    get_DAB_mapper_ref(carrier_mapper, params.nb_data_carriers, params.nb_fft);

    // create our single ofdm frame
    const int frame_size = 
        params.nb_null_period +
        params.nb_symbol_period*params.nb_frame_symbols;
    auto frame_out_buf = new std::complex<float>[frame_size];
    auto frame_tx_buf = new std::complex<uint8_t>[frame_size];
    
    // determine the number of bits that the ofdm frame contains
    // a single carrier contains 2 bits (there are four possible dqpsk phases)
    // the PRS (phase reference symbol) doesnt contain any information
    const int nb_frame_bits = 
        (params.nb_frame_symbols-1)*params.nb_data_carriers*2;
    const int nb_frame_bytes = nb_frame_bits/8;
    auto frame_bytes_buf = new uint8_t[nb_frame_bytes];

    uint16_t scrambler_code_word = 0b0000000010101001;
    auto scrambler = Scrambler();
    scrambler.Reset();
    for (int i = 0; i < nb_frame_bytes; i++) {
        frame_bytes_buf[i] = scrambler.Process();
    }

    // if we are only interested in printing the source data
    if (print_sample_message) {
        fprintf(stderr, "Outputing %d bytes\n", nb_frame_bytes);
        fwrite(frame_bytes_buf, sizeof(uint8_t), nb_frame_bytes, stdout);
        return 0;
    }

    // perform OFDM modulation 
    auto ofdm_mod = OFDM_Modulator(params, prs_fft_ref);
    auto res = ofdm_mod.ProcessBlock(frame_out_buf, frame_size, frame_bytes_buf, nb_frame_bytes);
    if (!res) {
        fprintf(stderr, "Failed to create the OFDM frame\n");
        return 1;
    }

    const float frequency_shift = 330.0f;
    ApplyFrequencyShift(frame_out_buf, frame_out_buf, frame_size, frequency_shift);

    for (int i = 0; i < frame_size; i++) {
        const float I = frame_out_buf[i].real();
        const float Q = frame_out_buf[i].imag();
        const float A = 1.0f/(float)params.nb_data_carriers * 200.0f * 4.0f;
        const uint8_t I0 = static_cast<uint8_t>(I*A + 128.0f);
        const uint8_t Q0 = static_cast<uint8_t>(Q*A + 128.0f);
        frame_tx_buf[i] = std::complex<uint8_t>(I0, Q0);
    }

    while (true) {
        fwrite(frame_tx_buf, sizeof(std::complex<uint8_t>), frame_size, stdout);
    }

    delete [] prs_fft_ref;
    delete [] carrier_mapper;

    return 0;
}
