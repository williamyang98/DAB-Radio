#include <stdio.h>
#define _USE_MATH_DEFINES
#include <cmath>
#include <complex>
#include <vector>

#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif

#include "utility/getopt/getopt.h"
#include "utility/span.h"

float ApplyFrequencyShift(
    tcb::span<const std::complex<uint8_t>> x, tcb::span<std::complex<uint8_t>> y, 
    const float frequency, float dt=0.0f, const float Ts=1.0f/2.048e6)
{
    const size_t N = x.size();
    for (int i = 0; i < N; i++) {
        auto pll = std::complex<float>(
            std::cos(dt),
            std::sin(dt));
        auto v_in = std::complex<float>(
            (float)(x[i].real()) - 128.0f,
            (float)(x[i].imag()) - 128.0f);
        auto v_out = v_in * pll;
        y[i] = std::complex<uint8_t>(
            (uint8_t)(v_out.real() + 128.0f),
            (uint8_t)(v_out.imag() + 128.0f));
        dt += 2.0f * (float)M_PI * frequency * Ts;
        // prevent precision errors when we have a large frequency
        dt = std::fmod(dt, 2.0f*(float)M_PI);
    }
    return dt;
}

void usage() {
    fprintf(stderr, 
        "simulate_transmitter, produces OFDM data as raw IQ values\n\n"
        "\t[-f frequency shift in Hz (default: 0)\n"
        "\t[-b block size (default: 8192)\n"
        "\t[-h (show usage)]\n"
    );
}

int main(int argc, char** argv) 
{

#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif

    float frequency_shift = 0;
    int block_size = 8192;

    int opt; 
    while ((opt = getopt(argc, argv, "f:b:h")) != -1) {
        switch (opt) {
        case 'f':
            frequency_shift = (float)(atof(optarg));
            break;
        case 'b':
            block_size = (int)(atof(optarg));
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

    auto rx_in = std::vector<std::complex<uint8_t>>(block_size);
    float dt = 0.0f;

    while (true) {
        const size_t N = rx_in.size();
        const size_t nb_read = fread(rx_in.data(), sizeof(std::complex<uint8_t>), N, stdin);
        if (nb_read != N) {
            fprintf(stderr, "Failed to read in block %zu/%zu\n", nb_read, N);
            break;
        }

        dt = ApplyFrequencyShift(rx_in, rx_in, frequency_shift, dt);

        const size_t nb_write = fwrite(rx_in.data(), sizeof(std::complex<uint8_t>), N, stdout);
        if (nb_write != N) {
            fprintf(stderr, "Failed to write out frame %zu/%zu\n", nb_write, N);
            break;
        }
    }

    return 0;
}
