#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#include <string>

#if _WIN32
#include <io.h>
#include <fcntl.h>
#endif

#include <argparse/argparse.hpp>
#include "dab/constants/dab_parameters.h"
#include "viterbi_config.h"
#include "./app_helpers/app_viterbi_convert_block.h"

void init_parser(argparse::ArgumentParser& parser) {
    parser.add_argument("-t", "--type")
        .choices("soft_to_hard", "hard_to_soft")
        .metavar("TYPE")
        .nargs(1).required()
        .help("Type of conversion to perform (soft_to_hard, hard_to_soft)");
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
    parser.add_argument("-n", "--block-size")
        .default_value(size_t(8192)).scan<'u', size_t>()
        .metavar("BLOCK_SIZE")
        .nargs(1).required()
        .help("Number of hard bytes to read/write at once");
}

struct Args {
    bool is_soft_to_hard;
    std::string input_filename;
    std::string output_filename;
    size_t block_size;
};

Args get_args_from_parser(const argparse::ArgumentParser& parser) {
    Args args;
    args.is_soft_to_hard = false;
    auto type = parser.get<std::string>("--type");
    if (type.compare("soft_to_hard") == 0) {
        args.is_soft_to_hard = true;
    }
    args.input_filename = parser.get<std::string>("--input");
    args.output_filename = parser.get<std::string>("--output");
    args.block_size = parser.get<size_t>("--block-size");
    return args;
}

int main(int argc, char** argv) {
    auto parser = argparse::ArgumentParser("convert_viterbi", "0.1.0");
    parser.add_description("Converts between viterbi soft bits and hard bytes");
    parser.add_epilog(
        "Use this to compress and decompress the output from the OFDM demodulator.\n"
        "Converting from viterbi soft bits to hard bytes will reduce space used by 8 times."
    );
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

    if (args.is_soft_to_hard) {
        auto bits_in = std::make_shared<InputFile<viterbi_bit_t>>(fp_in);
        auto bytes_out = std::make_shared<OutputFile<uint8_t>>(fp_out);
        auto convert_bits_to_bytes = std::make_shared<Convert_Viterbi_Bits_to_Bytes>();
        convert_bits_to_bytes->set_input_stream(bits_in);
        auto buf_bytes = std::vector<uint8_t>(args.block_size);
        bool is_running = true;
        while (is_running) {
            const size_t total_read = convert_bits_to_bytes->read(buf_bytes);
            if (total_read != buf_bytes.size()) {
                is_running = false;
            }
            const auto write_buf = tcb::span(buf_bytes).first(total_read);
            const size_t total_written = bytes_out->write(write_buf);
            if (total_written != total_read) {
                is_running = false;
            }
        }
    } else {
        auto bytes_in = std::make_shared<InputFile<uint8_t>>(fp_in);
        auto bits_out = std::make_shared<OutputFile<viterbi_bit_t>>(fp_out);
        auto convert_bytes_to_bits = std::make_shared<Convert_Viterbi_Bytes_to_Bits>();
        convert_bytes_to_bits->set_input_stream(bytes_in);
        auto buf_bits = std::vector<viterbi_bit_t>(args.block_size*8);
        bool is_running = true;
        while (is_running) {
            const size_t total_read = convert_bytes_to_bits->read(buf_bits);
            if (total_read != buf_bits.size()) {
                is_running = false;
            }
            const auto write_buf = tcb::span(buf_bits).first(total_read);
            const size_t total_written = bits_out->write(write_buf);
            if (total_written != total_read) {
                is_running = false;
            }
        }
    }

    return 0;
}