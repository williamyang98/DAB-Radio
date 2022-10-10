#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include <io.h>
#include <fcntl.h>

#include "./getopt/getopt.h"

#include "fic_processor.h"

#define PRINT_LOG 1

#if PRINT_LOG 
  #define LOG_MESSAGE(...) fprintf(stderr, ##__VA_ARGS__)
#else
  #define LOG_MESSAGE(...) (void)0
#endif

void ProcessFrame(const uint8_t* buf);

void usage() {
    fprintf(stderr, 
        "process_frames, decoded DAB frame data\n\n"
        "\t[-i input filename (default: None)]\n"
        "\t    If no file is provided then stdin is used\n"
        "\t[-h (show usage)]\n"
    );
}

int main(int argc, char** argv) {
    char* rd_filename = NULL;

    int opt; 
    while ((opt = getopt(argc, argv, "i:h")) != -1) {
        switch (opt) {
        case 'i':
            rd_filename = optarg;
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

    _setmode(_fileno(fp_in), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);

    const int N = 28800;
    auto buf = new uint8_t[N];


    while (true) {
        const auto nb_read = fread(buf, sizeof(uint8_t), N, fp_in);
        if (nb_read != N) {
            fprintf(stderr, "Failed to read %d bytes\n", N);
            return 1;
        }
        ProcessFrame(buf);
    }

    return 0;
}

void ProcessFrame(const uint8_t* buf) {
    const int nb_frame_length = 28800;
    const int nb_symbols = 75;
    const int nb_sym_length = nb_frame_length / nb_symbols;

    const int nb_fic_symbols = 3;

    const auto* fic_buf = &buf[0];
    const auto* msc_buf = &buf[nb_fic_symbols*nb_sym_length];

    const int nb_fic_length = nb_sym_length*nb_fic_symbols;
    const int nb_msc_length = nb_sym_length*(nb_symbols-nb_fic_symbols);

    static auto fic_processor = FIC_Processor();

    // FIC: 3 symbols -> 12 FIBs -> 4 FIB groups
    // A FIB group contains FIGs (fast information group)
    const int nb_fic_groups = 4;
    const int nb_fic_group_length = nb_fic_length / nb_fic_groups;
    for (int i = 0; i < nb_fic_groups; i++) {
        const auto* fic_group_buf = &fic_buf[i*nb_fic_group_length];
        fic_processor.ProcessFIBGroup(fic_group_buf, i);
    }
}
