#include <stdio.h>
#define _USE_MATH_DEFINES
#include <cmath>
#include <complex>
#include <vector>
#include "utility/span.h"

#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif

#include "ofdm/dsp/apply_pll.h"
#include "./getopt/getopt.h"

constexpr float DC_LEVEL = 127.0f;
constexpr float SCALE = 128.0f;
constexpr float Fs = 2.048e6f;
constexpr float Ts = 1.0f/Fs;

void ByteToFloat(tcb::span<const std::complex<uint8_t>> x, tcb::span<std::complex<float>> y);
void FloatToByte(tcb::span<const std::complex<float>> x, tcb::span<std::complex<uint8_t>> y);

void usage() {
    fprintf(stderr, 
        "apply_frequency_shift, applies frequency shift to raw IQ values\n\n"
        "\t[-f frequency shift in Hz (default: 0)\n"
        "\t[-b block size (default: 8192)\n"
        "\t[-i input filename (default: None)]\n"
        "\t    If no file is provided then stdin is used\n"
        "\t[-o output filename (default: None)]\n"
        "\t    If no file is provided then stdout is used\n"
        "\t[-h (show usage)]\n"
    );
}

int main(int argc, char** argv) {
    float frequency_shift = 0;
    int block_size = 8192;
    char* rd_filename = NULL;
    char* wr_filename = NULL;

    int opt; 
    while ((opt = getopt_custom(argc, argv, "f:b:i:o:h")) != -1) {
        switch (opt) {
        case 'f':
            frequency_shift = (float)(atof(optarg));
            break;
        case 'b':
            block_size = (int)(atof(optarg));
            break;
        case 'i':
            rd_filename = optarg;
            break;
        case 'o':
            wr_filename = optarg;
            break;
        case 'h':
        default:
            usage();
            return 0;
        }
    }

    if (block_size <= 0) {
        fprintf(stderr, "Block size must be positive got %d\n", block_size);
        return 1;
    }

    const float max_frequency_shift = 100e3f;
    if ((frequency_shift < -max_frequency_shift) || (frequency_shift > max_frequency_shift)) {
        fprintf(stderr, "Frequency shift our of maximum range |%.2f| > %.2f\n", frequency_shift, max_frequency_shift);
        return 1;
    }

    FILE* fp_in = stdin;
    if (rd_filename != NULL) {
        fp_in = fopen(rd_filename, "rb");
        if (fp_in == NULL) {
            fprintf(stderr, "Failed to open file for reading\n");
            return 1;
        }
    }

    FILE* fp_out = stdout;
    if (wr_filename != NULL) {
        fp_out = fopen(wr_filename, "wb+");
        if (fp_out == NULL) {
            fprintf(stderr, "Failed to open file for writing\n");
            return 1;
        }
    }

#ifdef _WIN32
    _setmode(_fileno(fp_in), _O_BINARY);
    _setmode(_fileno(fp_out), _O_BINARY);
#endif

    auto rx_in = std::vector<std::complex<uint8_t>>(block_size);
    auto rx_float = std::vector<std::complex<float>>(block_size);
    float dt = 0.0f;
    while (true) {
        const size_t N = rx_in.size();
        const size_t nb_read = fread(rx_in.data(), sizeof(std::complex<uint8_t>), N, fp_in);
        if (nb_read != N) {
            fprintf(stderr, "Failed to read in block %zu/%zu\n", nb_read, N);
            break;
        }

        ByteToFloat(rx_in, rx_float);

        apply_pll_auto(rx_float, rx_float, frequency_shift, dt);
        // NOTE: Manually compute the next dt since floating point inaccuracies result in 
        //       a discontinuity
        dt += 2.0f * (float)M_PI *  frequency_shift * Ts * (float)N;
        dt = std::fmod(dt, 2.0f*(float)M_PI);

        FloatToByte(rx_float, rx_in);

        const size_t nb_write = fwrite(rx_in.data(), sizeof(std::complex<uint8_t>), N, fp_out);
        if (nb_write != N) {
            fprintf(stderr, "Failed to write out frame %zu/%zu\n", nb_write, N);
            break;
        }
    }

    return 0;
}

void ByteToFloat(tcb::span<const std::complex<uint8_t>> x, tcb::span<std::complex<float>> y) {
    const size_t N = x.size();
    for (int i = 0; i < N; i++) {
        y[i] = std::complex<float>(
            (float)(x[i].real()) - DC_LEVEL,
            (float)(x[i].imag()) - DC_LEVEL);
        y[i] /= SCALE;
    }
}

void FloatToByte(tcb::span<const std::complex<float>> x, tcb::span<std::complex<uint8_t>> y) {
    const size_t N = x.size();
    for (int i = 0; i < N; i++) {
        y[i] = std::complex<uint8_t>(
            (uint8_t)(x[i].real()*SCALE + DC_LEVEL),
            (uint8_t)(x[i].imag()*SCALE + DC_LEVEL));
    }
}
