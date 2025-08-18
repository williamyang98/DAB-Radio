#include <stdint.h>
#include <stdio.h>
#include <complex>
#include <exception>
#include <iostream>
#include <string>
#include <vector>
#include "utility/span.h"

#if _WIN32
#include <io.h>
#include <fcntl.h>
#endif

#include <argparse/argparse.hpp>
#include "app_helpers/app_readers.h"
#include "ofdm/dab_mapper_ref.h"
#include "ofdm/dab_ofdm_params_ref.h"
#include "ofdm/dab_prs_ref.h"
#include "ofdm/dsp/apply_pll.h"
#include "ofdm/ofdm_modulator.h"
#include "ofdm/ofdm_params.h"

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

template <typename T>
void write_frame_to_file(
    FILE* fp_out,
    tcb::span<const std::complex<float>> data, float scale,
    const bool is_little_endian
) {
    // rescale for quantisation
    scale *= RawIQ<T>::MAX_AMPLITUDE;

    // perform quantisation
    auto quantised = std::vector<RawIQ<T>>(data.size());
    for (size_t i = 0; i < data.size(); i++) {
        const float I = data[i].real();
        const float Q = data[i].imag();
        quantised[i] = RawIQ<T>::from_iq(I*scale, Q*scale);
    }

    const bool reverse_endian = get_is_machine_little_endian() != is_little_endian;
    if (reverse_endian) {
        auto components = tcb::span<T>(
            reinterpret_cast<T*>(quantised.data()),
            2*quantised.size()
        );
        reverse_endian_inplace(components);
    }

    while (true) {
        const size_t N = quantised.size();
        const size_t nb_write = fwrite(quantised.data(), sizeof(RawIQ<T>), N, fp_out);
        if (nb_write != N) {
            fprintf(stderr, "Failed to write out frame %zu/%zu\n", nb_write, N);
            break;
        }
    }
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

    // determine the number of bits that the ofdm frame contains
    // a single carrier contains 2 bits (there are four possible dqpsk phases)
    // the PRS (phase reference symbol) doesnt contain any information
    const size_t nb_frame_bits = (params.nb_frame_symbols-1)*params.nb_data_carriers*2;
    const size_t nb_frame_bytes = nb_frame_bits/8;

    // generate random digital data
    auto frame_bytes_buf = std::vector<uint8_t>(nb_frame_bytes);
    auto scrambler = Scrambler();
    scrambler.Reset();
    for (size_t i = 0; i < nb_frame_bytes; i++) {
        frame_bytes_buf[i] = scrambler.Process();
    }

    // perform OFDM modulation 
    auto ofdm_mod = OFDM_Modulator(params, prs_fft_ref);
    auto frame_out_buf = std::vector<std::complex<float>>(frame_size);
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

    const float scale = 1.0f/(float)params.nb_data_carriers * 4.0f;
    const bool is_little_endian = true;
    write_frame_to_file<uint8_t>(fp_out, frame_out_buf, scale, is_little_endian);
    fclose(fp_out);
    return 0;
}
