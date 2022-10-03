#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include <io.h>
#include <fcntl.h>

#include "./getopt/getopt.h"

#include <complex>
#include "ofdm_demodulator.h"
#include "dab_ofdm_params_ref.h"
#include "dab_prs_ref.h"

#define PRINT_LOG 1
#if PRINT_LOG 
  #define LOG_MESSAGE(...) fprintf(stderr, ##__VA_ARGS__)
#else
  #define LOG_MESSAGE(...) (void)0
#endif

void usage() {
    fprintf(stderr, 
        "read_data, runs OFDM demodulation on raw IQ values\n\n"
        "\t[-b block size (default: 8192)]\n"
        "\t[-i input filename (default: None)]\n"
        "\t    If no file is provided then stdin is used\n"
        "\t[-M dab transmission mode (default: 1)]\n"
        "\t[-h (show usage)]\n"
    );
}

int main(int argc, char** argv) 
{
    int block_size = 8192;
    int transmission_mode = 1;
    char* rd_filename = NULL;

    int opt; 
    while ((opt = getopt(argc, argv, "b:i:M:h")) != -1) {
        switch (opt) {
        case 'b':
            block_size = (int)(atof(optarg));
            if (block_size <= 0) {
                fprintf(stderr, "Block size must be positive (%d)\n", block_size); 
                return 1;
            }
            break;
        case 'i':
            rd_filename = optarg;
            break;
        case 'M':
            transmission_mode = (int)(atof(optarg));
            if (transmission_mode <= 0 || transmission_mode > 4) {
                fprintf(stderr, "Transmission modes: I,II,III,IV are supported not (%d)\n", transmission_mode);
                return 1;
            }
            break;
        case 'h':
        default:
            usage();
            return 0;
        }
    }

    // app startup
    FILE* fp_in = stdin;
    if (rd_filename != NULL) {
        errno_t err = fopen_s(&fp_in, rd_filename, "r");
        if (err != 0) {
            LOG_MESSAGE("Failed to open file for reading\n");
            return 1;
        }
    }

    auto buf_rd = new std::complex<uint8_t>[block_size];
    auto buf_rd_raw = new std::complex<float>[block_size];

    _setmode(_fileno(fp_in), _O_BINARY);
    
    const OFDM_Params ofdm_params = get_DAB_OFDM_params(transmission_mode);
    auto ofdm_prs_ref = new std::complex<float>[ofdm_params.nb_fft];
    get_DAB_PRS_reference(transmission_mode, ofdm_prs_ref, ofdm_params.nb_fft);
    auto ofdm_demod = OFDM_Demodulator(ofdm_params, ofdm_prs_ref);
    delete [] ofdm_prs_ref;

    while (true) {
        auto nb_read = fread((void*)buf_rd, sizeof(std::complex<uint8_t>), block_size, fp_in);
        if (nb_read != block_size) {
            fprintf(stderr, "Failed to read in data\n");
            break;
        }

        for (int i = 0; i < block_size; i++) {
            auto& v = buf_rd[i];
            const float I = static_cast<float>(v.real()) - 127.5f;
            const float Q = static_cast<float>(v.imag()) - 127.5f;
            buf_rd_raw[i] = std::complex<float>(I, Q);
        }

        ofdm_demod.ProcessBlock(buf_rd_raw, block_size);
    }

    delete [] buf_rd;
    delete [] buf_rd_raw;

    return 0;
}