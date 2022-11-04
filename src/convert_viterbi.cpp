#include <stdio.h>
#include <stdlib.h>

#include <io.h>
#include <fcntl.h>

#include "getopt/getopt.h"
#include "dab/constants/dab_parameters.h"
#include "dab/algorithms/viterbi_decoder.h"
#include "dab/algorithms/phil_karn_viterbi_decoder.h"

#define PRINT_LOG 1
#if PRINT_LOG 
  #define LOG_MESSAGE(...) fprintf(stderr, ##__VA_ARGS__)
#else
  #define LOG_MESSAGE(...) (void)0
#endif

void usage() {
    fprintf(stderr, 
        "convert_viterbi,\n"
        "   Converts between soft decision bits and hard decision bytes for viterbi decoding\n"
        "   This is useful if we want to store OFDM frames in a more compact byte format\n\n"
        "\t[-d (decodes hard decision bytes to soft decision bits)]\n"
        "\t     Default mode is encode soft decision bits as hard decision bytes\n"
        "\t[-i input filename (default: None)]\n"
        "\t    If no file is provided then stdin is used\n"
        "\t[-o output filename (default: None)]\n"
        "\t    If no file is provided then stdout is used\n"
        "\t[-M dab transmission mode (default: 1)]\n"
        "\t[-h (show usage)]\n"
    );
}

void DecodeBytesToBits(const uint8_t* bytes, viterbi_bit_t* bits, const int nb_bytes);
void EncodeBitsToBytes(const viterbi_bit_t* bits, uint8_t* bytes, const int nb_bits);

int main(int argc, char** argv) {
    int transmission_mode = 1;
    bool is_decode = false;
    char* rd_filename = NULL;
    char* wr_filename = NULL;

    int opt; 
    while ((opt = getopt(argc, argv, "di:o:M:h")) != -1) {
        switch (opt) {
        case 'd':
            is_decode = true;
            break;
        case 'i':
            rd_filename = optarg;
            break;
        case 'o':
            wr_filename = optarg;
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

    FILE* fp_out = stdout;
    if (wr_filename != NULL) {
        errno_t err = fopen_s(&fp_out, wr_filename, "w");
        if (err != 0) {
            LOG_MESSAGE("Failed to open file for writing\n");
            return 1;
        }
    }

    _setmode(_fileno(fp_in), _O_BINARY);
    _setmode(_fileno(fp_out), _O_BINARY);
    
    const DAB_Parameters params = get_dab_parameters(transmission_mode);
    const int nb_bits = params.nb_frame_bits; 
    const int nb_bytes = nb_bits/8;

    auto* soft_bits = new viterbi_bit_t[nb_bits];
    auto* hard_bytes = new uint8_t[nb_bytes];

    // Convert hard bytes to soft bits
    if (is_decode) {
        while (true) {
            auto nb_read = fread((void*)hard_bytes, sizeof(uint8_t), nb_bytes, fp_in);
            if (nb_read != nb_bytes) {
                fprintf(stderr, "Failed to read in hard bytes\n");
                break;
            }

            DecodeBytesToBits(hard_bytes, soft_bits, nb_bytes);
            auto nb_write = fwrite((void*)soft_bits, sizeof(viterbi_bit_t), nb_bits, fp_out);
            if (nb_write != nb_bits) {
                fprintf(stderr, "Failed to write out soft bits\n");
                break;
            }
        }
    // Convert soft bits to hard bytes
    } else {
        while (true) {
            auto nb_read = fread((void*)soft_bits, sizeof(viterbi_bit_t), nb_bits, fp_in);
            if (nb_read != nb_bits) {
                fprintf(stderr, "Failed to read in soft bits\n");
                break;
            }

            EncodeBitsToBytes(soft_bits, hard_bytes, nb_bits);
            auto nb_write = fwrite((void*)hard_bytes, sizeof(uint8_t), nb_bytes, fp_out);
            if (nb_write != nb_bytes) {
                fprintf(stderr, "Failed to write out hard bytes\n");
                break;
            }
        }
    }

    return 0;
}

void DecodeBytesToBits(const uint8_t* bytes, viterbi_bit_t* bits, const int nb_bytes) {
    int curr_bit = 0;
    for (int i = 0; i < nb_bytes; i++) {
        for (int j = 0; j < 8; j++) {
            uint8_t v = (bytes[i] >> j) & 0b1;
            bits[curr_bit++] = v ? SOFT_DECISION_HIGH : SOFT_DECISION_LOW;
        }
    }
}

void EncodeBitsToBytes(const viterbi_bit_t* bits, uint8_t* bytes, const int nb_bits) {
    const viterbi_bit_t mid = (SOFT_DECISION_HIGH+SOFT_DECISION_LOW)/2;

    int curr_byte = 0;
    for (int i = 0; i < nb_bits; i+=8) {
        uint8_t v = 0;
        for (int j = 0; j < 8; j++) {
            uint8_t b = (bits[i+j] >= mid) ? 1 : 0;
            v |= (b << j);
        }
        bytes[curr_byte++] = v;
    }
}