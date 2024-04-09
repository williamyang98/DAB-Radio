#include <stdint.h>
#include <stdio.h>
#include <exception>
#include <iostream>
#include <string>
#include <vector>

#if _WIN32
#include <io.h>
#include <fcntl.h>
#endif

#include <argparse/argparse.hpp>

void init_parser(argparse::ArgumentParser& parser) {
    parser.add_argument("input")
        .metavar("INPUT_FILENAME")
        .nargs(1).required()
        .help("Filename of input to converter");
    parser.add_argument("-o", "--output")
        .default_value(std::string(""))
        .metavar("OUTPUT_FILENAME")
        .nargs(1).required()
        .help("Filename of output from converter (defaults to stdout)");
    parser.add_argument("-n", "--block-size")
        .default_value(size_t(8192)).scan<'u', size_t>()
        .metavar("BLOCK_SIZE")
        .nargs(1).required()
        .help("Number of bytes to read from the wav file in chunks");
}

struct Args {
    std::string input_filename;
    std::string output_filename;
    size_t block_size;
};

Args get_args_from_parser(const argparse::ArgumentParser& parser) {
    Args args;
    args.input_filename = parser.get<std::string>("input");
    args.output_filename = parser.get<std::string>("--output");
    args.block_size = parser.get<size_t>("--block-size");
    return args;
}

int main(int argc, char** argv) {
    auto parser = argparse::ArgumentParser("loop_file", "0.1.0");
    parser.add_description("Reads a file in a loop and echoes it to stdout or a file");
    parser.add_epilog("Useful for replaying data in an infinite loop to a program");
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

    FILE* fp_in = fopen(args.input_filename.c_str(), "rb");
    if (fp_in == nullptr) {
        fprintf(stderr, "Failed to open input file: '%s'\n", args.input_filename.c_str());
        return 1;
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
    auto block = std::vector<uint8_t>(N);

    while (true) {
        const size_t nb_read = fread(block.data(), sizeof(uint8_t), N, fp_in);
        const size_t nb_write = fwrite(block.data(), sizeof(uint8_t), nb_read, fp_out);
        if (nb_write != nb_read) {
            fprintf(stderr, "Failed to write out block %zu/%zu bytes. Exiting...\n", nb_write, nb_read);
            break;
        }
        if (nb_read != N) {
            fseek(fp_in, 0, SEEK_SET);
        }
    }
    return 0;
}
