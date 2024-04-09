#define _USE_MATH_DEFINES
#include <cmath>
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
#include "ofdm/dsp/apply_pll.h"

constexpr float DC_LEVEL = 127.0f;
constexpr float SCALE = 128.0f;

struct RawIQ {
    uint8_t I;
    uint8_t Q;
};

static std::complex<float> raw_iq_to_c32(RawIQ x) {
    return std::complex<float>(
        (float(x.I) - DC_LEVEL) / SCALE,
        (float(x.Q) - DC_LEVEL) / SCALE
    );
}

static RawIQ c32_to_raw_iq(std::complex<float> x) {
    RawIQ y;
    y.I = uint8_t(x.real()*SCALE + DC_LEVEL);
    y.Q = uint8_t(x.imag()*SCALE + DC_LEVEL);
    return y;
}

void init_parser(argparse::ArgumentParser& parser) {
    parser.add_argument("-f", "--frequency")
        .default_value(float(0.0f)).scan<'g', float>()
        .metavar("FREQUENCY")
        .nargs(1).required()
        .help("Amount of Hz to shift 8bit IQ signal");
    parser.add_argument("-s", "--sampling-rate")
        .default_value(float(2'048'000)).scan<'g', float>()
        .metavar("SAMPLING_RATE")
        .nargs(1).required()
        .help("Sampling rate of data in Hz");
    parser.add_argument("-n", "--block-size")
        .default_value(size_t(8192)).scan<'u', size_t>()
        .metavar("BLOCK_SIZE")
        .nargs(1).required()
        .help("Number of IQ samples to read at once");
    parser.add_argument("-i", "--input")
        .default_value(std::string(""))
        .metavar("INPUT_FILENAME")
        .nargs(1).required()
        .help("Filename of input to converter (defaults to stdin)");
    parser.add_argument("-o", "--output")
        .default_value(std::string(""))
        .metavar("OUTPUT_FILENAME")
        .nargs(1).required()
        .help("Filename of output from converter (defaults to stdout)");
}

struct Args {
    float frequency;
    float sampling_rate;
    size_t block_size;
    std::string input_filename;
    std::string output_filename;
};

Args get_args_from_parser(const argparse::ArgumentParser& parser) {
    Args args;
    args.frequency = parser.get<float>("--frequency");
    args.sampling_rate = parser.get<float>("--sampling-rate");
    args.block_size = parser.get<size_t>("--block-size");
    args.input_filename = parser.get<std::string>("--input");
    args.output_filename = parser.get<std::string>("--output");
    return args;
}

int main(int argc, char** argv) {
    auto parser = argparse::ArgumentParser("apply_frequency_shift", "0.1.0");
    parser.add_description("Shifts an 8bit IQ signal by a set frequency");
    init_parser(parser);
    try {
        parser.parse_args(argc, argv);
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << std::endl;
        std::cerr << parser;
        return 1;
    }
    const auto args = get_args_from_parser(parser);

    if (args.sampling_rate <= 0.0f) {
        fprintf(stderr, "Sampling rate must be positive (%.3f)\n", args.sampling_rate);
        return 1;
    }

    if (args.block_size == 0) {
        fprintf(stderr, "Block size cannot be zero\n");
        return 1;
    }

    FILE* fp_in = stdin;
    if (!args.input_filename.empty()) { 
        fp_in = fopen(args.input_filename.c_str(), "rb");
        if (fp_in == nullptr) {
            fprintf(stderr, "Failed to open input file: '%s'\n", args.input_filename.c_str());
            return 1;
        }
    }

    FILE* fp_out = stdout;
    if (!args.output_filename.empty()) {
        fp_out = fopen(args.output_filename.c_str(), "wb+");
        if (fp_out == nullptr) {
            fprintf(stderr, "Failed to open output file: '%s'\n", args.output_filename.c_str());
            return 1;
        }
    }

#if _WIN32
    _setmode(_fileno(fp_in), _O_BINARY);
    _setmode(_fileno(fp_out), _O_BINARY);
#endif

    const size_t N = args.block_size;
    const float frequency_shift = args.frequency / args.sampling_rate;
    auto rx_in = std::vector<RawIQ>(N);
    auto rx_float = std::vector<std::complex<float>>(N);
    float dt = 0.0f;
    while (true) {
        const size_t nb_read = fread(rx_in.data(), sizeof(RawIQ), N, fp_in);
        if (nb_read != N) {
            fprintf(stderr, "Failed to read in block %zu/%zu\n", nb_read, N);
            break;
        }
        for (size_t i = 0; i < N; i++) {
            rx_float[i] = raw_iq_to_c32(rx_in[i]);
        }

        apply_pll_auto(rx_float, rx_float, frequency_shift, dt);
        dt += float(N)*frequency_shift;
        dt = dt - std::round(dt);
 
        for (size_t i = 0; i < N; i++) {
            rx_in[i] = c32_to_raw_iq(rx_float[i]);
        }
        const size_t nb_write = fwrite(rx_in.data(), sizeof(RawIQ), N, fp_out);
        if (nb_write != N) {
            fprintf(stderr, "Failed to write out frame %zu/%zu\n", nb_write, N);
            break;
        }
    }

    return 0;
}
