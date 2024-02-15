#include <stdio.h>
#include <stdint.h>
#include <vector>
#include <string.h>
#include "utility/span.h"

#if _WIN32
#include <io.h>
#include <fcntl.h>
#endif

#include <argparse/argparse.hpp>

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
};

static bool validate_wav_header(WavHeader& header);

void init_parser(argparse::ArgumentParser& parser) {
    parser.add_argument("-n", "--block-size")
        .default_value(size_t(8192)).scan<'u', size_t>()
        .metavar("BLOCK_SIZE")
        .nargs(1).required()
        .help("Number of bytes to read from the wav file in chunks");
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
    size_t block_size;
    std::string input_filename;
    std::string output_filename;
};

Args get_args_from_parser(const argparse::ArgumentParser& parser) {
    Args args;
    args.block_size = parser.get<size_t>("--block-size");
    args.input_filename = parser.get<std::string>("--input");
    args.output_filename = parser.get<std::string>("--output");
    return args;
}


int main(int argc, char** argv) {
    auto parser = argparse::ArgumentParser("read_wav", "0.1.0");
    parser.add_description("Reads a wav file and outputs raw data");
    parser.add_epilog("Useful for reading captured radio 8bit IQ data that was stored in a wav file");
    init_parser(parser);
    try {
        parser.parse_args(argc, argv);
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << std::endl;
        std::cerr << parser;
        return 1;
    }
    const auto args = get_args_from_parser(parser);

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

    const size_t N = args.block_size;
    auto block = std::vector<uint8_t>(N);

    bool is_running = true;
    while (is_running) {
        // convert 16bit to 8bit
        if (is_16_bit) {
            static auto convert_buf = std::vector<int16_t>(N);
            nb_read = fread(convert_buf.data(), sizeof(int16_t), N, fp_in);
            for (size_t i = 0; i < nb_read; i++) {
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
 
    (void)is_warning;
    return !is_error;
}