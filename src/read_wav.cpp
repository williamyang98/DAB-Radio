#include <stdio.h>
#include <stdint.h>
#include <vector>
#include <string.h>

#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif

#include "utility/getopt/getopt.h"
#include "utility/span.h"

// Source: http://soundfile.sapp.org/doc/WaveFormat/
struct WavHeader {
    char     ChunkID[4];
    int32_t  ChunkSize;
    char     Format[4];
    // Subchunk 1 = format information
    char     Subchunk1ID[4];
    int32_t  Subchunk1Size;
    int16_t  AudioFormat;
    int16_t  NumChannels;
    int32_t  SampleRate;
    int32_t  ByteRate;
    int16_t  BlockAlign;
    int16_t  BitsPerSample;
    // Subchunk 2 = data 
    char     Subchunk2ID[4];
    int32_t  Subchunk2Size;
} header;

bool validate_wav_header(WavHeader& header);

void usage() {
    fprintf(stderr, 
        "read_wav, Reads a wav file and dumps data to output\n\n"
        "\t[-b block size (default: 65536)\n"
        "\t[-i input filename (default: None)]\n"
        "\t    If no file is provided then stdin is used\n"
        "\t[-o output filename (default: None)]\n"
        "\t    If no file is provided then stdout is used\n"
        "\t[-h (show usage)]\n"
    );
}

int main(int argc, char** argv) {
    int block_size = 65536;
    char* rd_filename = NULL;
    char* wr_filename = NULL;

    int opt; 
    while ((opt = getopt_custom(argc, argv, "b:i:o:h")) != -1) {
        switch (opt) {
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

    size_t nb_read;

    WavHeader header;
    const size_t header_size = sizeof(header);
    nb_read = fread(&header, sizeof(uint8_t), header_size, fp_in);
    if (nb_read != header_size) {
        fprintf(stderr, "Failed to read in wav header %zu/%zu bytes\n", nb_read, header_size);
        return 1;
    }

    if (!validate_wav_header(header)) {
        return 1;
    }

    if ((header.BitsPerSample != 8) && (header.BitsPerSample != 16)) {
        fprintf(stderr, "Expected either a 8bit or 16bit pcm file but got %d bits\n", header.BitsPerSample);
        return 1;
    }

    fprintf(stderr, "WAV file indicated %d bytes\n", header.ChunkSize);

    bool is_16_bit = (header.BitsPerSample == 16);
    if (is_16_bit) {
        fprintf(stderr, "Running conversion from 16bit to 8bit pcm\n");
    }

    const size_t N = (size_t)block_size;
    auto block = std::vector<uint8_t>(N);

    bool is_running = true;
    while (is_running) {
        // convert 16bit to 8bit
        if (is_16_bit) {
            static auto convert_buf = std::vector<int16_t>(N);
            nb_read = fread(convert_buf.data(), sizeof(int16_t), N, fp_in);
            for (int i = 0; i < nb_read; i++) {
                const int16_t v0 = convert_buf[i];
                block[i] = (uint8_t)(v0/256 + 127);
            }
        // stream 8bit directly
        } else {
            nb_read = fread(block.data(), sizeof(uint8_t), N, fp_in);
        }

        if (nb_read != N) {
            fprintf(stderr, "Failed to read in block %zu/%zu bytes\n", nb_read, N);
            is_running = false;
        }

        if (nb_read == 0) {
            break;
        }

        const size_t nb_write = fwrite(block.data(), sizeof(uint8_t), nb_read, fp_out);
        if (nb_write != nb_read) {
            fprintf(stderr, "Failed to write out block %zu/%zu bytes\n", nb_write, nb_read);
            is_running = false;
        }
    }

    return 0;
}

bool validate_wav_header(WavHeader& header) {
    bool is_error = false;
    bool is_warning = false;

    if (strncmp(header.ChunkID, "RIFF", 4) != 0) {
        fprintf(stderr, "[ERROR] Invalid wave header ChunkID: %.4s != RIFF\n", header.ChunkID);
        is_error = true;
    }

    if (strncmp(header.Format, "WAVE", 4) != 0) {
        fprintf(stderr, "[ERROR] Invalid wave header Format: %.4s != WAVE\n", header.Format);
        is_error = true;
    }

    if (strncmp(header.Subchunk1ID, "fmt ", 4) != 0) {
        fprintf(stderr, "[ERROR] Invalid wave header Subchunk1ID: %.4s != fmt \n", header.Format);
        is_error = true;
    }

    if (strncmp(header.Format, "WAVE", 4) != 0) {
        fprintf(stderr, "[ERROR] Invalid wave header Subchunk2ID: %.4s != data\n", header.Format);
        is_error = true;
    }

    if (header.AudioFormat != 1) {
        fprintf(stderr, "[WARN] Expected PCM format (1) but got %d\n", header.AudioFormat);
        is_warning = true;
    }

    const int32_t Fs_expected = 2048000;
    if (header.SampleRate != Fs_expected) {
        fprintf(stderr, "[WARN] Expected a sampling rate of %d but got %d\n", Fs_expected, header.SampleRate);
        is_warning = true;
    }

    if (header.NumChannels != 2) {
        fprintf(stderr, "[WARN] Expected 2 channels for complex IQ stream but got %d channels\n", header.NumChannels);
        is_warning = true;
    }

    if (header.BitsPerSample != 8) {
        fprintf(stderr, "[WARN] Expected 8bits per sample for an 8bit IQ stream but got %d bits\n", header.BitsPerSample);
        is_warning = true;
    }

    return !is_error;
}