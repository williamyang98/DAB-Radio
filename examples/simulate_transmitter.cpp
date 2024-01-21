#include <stdio.h>
#define _USE_MATH_DEFINES
#include <cmath>
#include <complex>
#include <vector>
#include "utility/span.h"

#if _WIN32
#include <io.h>
#include <fcntl.h>
#endif

#include <argparse/argparse.hpp>
#include "ofdm/ofdm_modulator.h"
#include "ofdm/ofdm_params.h"
#include "ofdm/dab_prs_ref.h"
#include "ofdm/dab_ofdm_params_ref.h"
#include "ofdm/dab_mapper_ref.h"
#include "ofdm/dsp/apply_pll.h"

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

template <typename T>
T clamp(T x, const T min, const T max) {
    T y = x;
    y = (y > min) ? y : min;
    y = (y > max) ? max : y;
    return y;
}

struct RawIQ {
    uint8_t I;
    uint8_t Q;
};

void init_parser(argparse::ArgumentParser& parser) {
    parser.add_argument("-m", "--transmission-mode")
        .default_value(int(1)).scan<'i', int>()
        .choices(1,2,3,4)
        .metavar("MODE")
        .nargs(1).required()
        .help("Dab transmission mode");
    parser.add_argument("-f", "--frequency")
        .default_value(float(0.0f)).scan<'g', float>()
        .metavar("FREQUENCY")
        .nargs(1).required()
        .help("Amount of Hz to shift 8bit IQ signal");
    parser.add_argument("-o", "--output")
        .default_value(std::string(""))
        .metavar("OUTPUT_FILENAME")
        .nargs(1).required()
        .help("Filename of output from converter (defaults to stdout)");
}

struct Args {
    int transmission_mode;
    float frequency;
    std::string output_filename;
};

Args get_args_from_parser(const argparse::ArgumentParser& parser) {
    Args args;
    args.transmission_mode = parser.get<int>("--transmission-mode");
    args.frequency = parser.get<float>("--frequency");
    args.output_filename = parser.get<std::string>("--output");
    return args;
}

int main(int argc, char** argv) {
    auto parser = argparse::ArgumentParser("simulate_transmitter", "0.1.0");
    parser.add_description("Simulates an OFDM transmitter sending random data");
    init_parser(parser);
    try {
        parser.parse_args(argc, argv);
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << std::endl;
        std::cerr << parser;
        return 1;
    }
    const auto args = get_args_from_parser(parser);

    FILE* fp_out = stdout;
    if (!args.output_filename.empty()) {
        fp_out = fopen(args.output_filename.c_str(), "wb+");
        if (fp_out == nullptr) {
            fprintf(stderr, "Failed to open output file: '%s'\n", args.output_filename.c_str());
            return 1;
        }
    }

#if _WIN32
    _setmode(_fileno(fp_out), _O_BINARY);
#endif

    const auto params = get_DAB_OFDM_params(args.transmission_mode);
    auto prs_fft_ref = std::vector<std::complex<float>>(params.nb_fft);
    auto carrier_mapper = std::vector<int>(params.nb_data_carriers);
    get_DAB_PRS_reference(args.transmission_mode, prs_fft_ref);
    get_DAB_mapper_ref(carrier_mapper, params.nb_fft);

    // create our single ofdm frame
    const size_t frame_size = params.nb_null_period + params.nb_symbol_period*params.nb_frame_symbols;
    auto frame_out_buf = std::vector<std::complex<float>>(frame_size);
    auto frame_tx_buf = std::vector<RawIQ>(frame_size);

    // determine the number of bits that the ofdm frame contains
    // a single carrier contains 2 bits (there are four possible dqpsk phases)
    // the PRS (phase reference symbol) doesnt contain any information
    const size_t nb_frame_bits = (params.nb_frame_symbols-1)*params.nb_data_carriers*2;
    const size_t nb_frame_bytes = nb_frame_bits/8;

    // generate random digital data
    auto frame_bytes_buf = std::vector<uint8_t>(nb_frame_bytes);
    uint16_t scrambler_code_word = 0b0000000010101001;
    auto scrambler = Scrambler();
    scrambler.Reset();
    for (int i = 0; i < nb_frame_bytes; i++) {
        frame_bytes_buf[i] = scrambler.Process();
    }

    // perform OFDM modulation 
    auto ofdm_mod = OFDM_Modulator(params, prs_fft_ref);
    auto res = ofdm_mod.ProcessBlock(frame_out_buf, frame_bytes_buf);
    if (!res) {
        fprintf(stderr, "Failed to create the OFDM frame\n");
        return 1;
    }
 
    if (args.frequency != 0.0f) {
        const float Fs = 2.048e6f; // DAB sampling frequency
        const float frequency_norm = args.frequency / Fs;
        apply_pll_auto(frame_out_buf, frame_out_buf, frequency_norm);
    }

    for (int i = 0; i < frame_size; i++) {
        const float I = frame_out_buf[i].real();
        const float Q = frame_out_buf[i].imag();
        const float A = 1.0f/(float)params.nb_data_carriers * 200.0f * 2.0f;
        const float I0 = clamp(I*A + 128.0f, 0.0f, 255.0f);
        const float Q0 = clamp(Q*A + 128.0f, 0.0f, 255.0f);
        const uint8_t I1 = static_cast<uint8_t>(I0);
        const uint8_t Q1 = static_cast<uint8_t>(Q0);
        frame_tx_buf[i] = RawIQ{ I1, Q1 };
    }

    while (true) {
        const size_t N = frame_tx_buf.size();
        const size_t nb_write = fwrite(frame_tx_buf.data(), sizeof(RawIQ), N, fp_out);
        if (nb_write != N) {
            fprintf(stderr, "Failed to write out frame %zu/%zu\n", nb_write, N);
            break;
        }
    }
    fclose(fp_out);
    return 0;
}
