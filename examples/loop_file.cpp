#include <stdint.h>
#include <stdio.h>
#include <exception>
#include <iostream>
#include <string>
#include <optional>
#include <vector>
#include <functional>
#include <memory>
#include "utility/span.h"
#include "./app_helpers/app_wav_reader.h"
#include "./app_helpers/app_io_buffers.h"

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
    parser.add_argument("-m", "--mode")
        .default_value(std::string("raw"))
        .metavar("MODE")
        .choices("raw", "wav_data", "wav_f32")
        .nargs(1).required()
        .help("Method of parsing file (raw, wav_data, wav_f32)");
}

struct Args {
    std::string input_filename;
    std::string output_filename;
    std::string mode;
    size_t block_size;
};

Args get_args_from_parser(const argparse::ArgumentParser& parser) {
    Args args;
    args.input_filename = parser.get<std::string>("input");
    args.output_filename = parser.get<std::string>("--output");
    args.mode = parser.get<std::string>("--mode");
    args.block_size = parser.get<size_t>("--block-size");
    return args;
}

int main(int argc, char** argv) {
    auto parser = argparse::ArgumentParser("loop_file", "0.1.0");
    parser.add_description("Reads a file in a loop and echoes it to stdout or a file");
    parser.add_epilog(
        "Useful for replaying data in an infinite loop to a program.\n"
        "    raw      - Loops binary file directly.\n"
        "    wav_data - Loops data chunk of wav file directly without any conversion.\n"
        "    wav_f32  - Converts data chunk into 32bit machine endian float and loops it."
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

    size_t block_size = args.block_size;

    std::function<size_t(tcb::span<uint8_t>)> read_bytes = nullptr;
    std::function<bool()> loop_file = nullptr;
    std::optional<size_t> loop_size = std::nullopt;
    if (args.mode == "raw") {
        read_bytes = [fp_in](tcb::span<uint8_t> dest) {
            return fread(dest.data(), sizeof(uint8_t), dest.size(), fp_in);
        };
        loop_file = [fp_in]() {
            const int rv = fseek(fp_in, 0, SEEK_SET);
            return rv == 0;
        };
        block_size = args.block_size;
    } else if (args.mode == "wav_data") {
        try {
            fprintf(stderr, "Reading wav header\n");
            const auto header = wav_read_header(fp_in);
            header.debug_print(stderr);
            const long loop_offset = static_cast<long>(header.data_chunk_offset);
            read_bytes = [fp_in](tcb::span<uint8_t> dest) {
                return fread(dest.data(), sizeof(uint8_t), dest.size(), fp_in);
            };
            loop_file = [fp_in, loop_offset]() {
                const int rv = fseek(fp_in, loop_offset, SEEK_SET);
                return rv == 0;
            };
            block_size = args.block_size;
        } catch (const std::exception& ex) {
            std::cerr << ex.what() << std::endl;
            return 1;
        }
    } else if (args.mode == "wav_f32") {
        try {
            fprintf(stderr, "Reading wav header\n");
            auto file = std::make_shared<FileWrapper>(fp_in);
            auto wav_reader = std::make_shared<WavFileReader>(file);
            auto byte_stream = std::make_shared<ReinterpretCastInputBuffer<uint8_t, float>>(wav_reader);
            const auto& header = wav_reader->get_header();
            header.debug_print(stderr);
            read_bytes = [byte_stream](tcb::span<uint8_t> dest) {
                return byte_stream->read(dest);
            };
            loop_file = [wav_reader]() {
                return wav_reader->loop();
            };
            // try to guarantee that we read a multiple of IQ sample
            constexpr size_t stride = sizeof(float)*2;
            block_size = (args.block_size/stride)*stride;
        } catch (const std::exception& ex) {
            std::cerr << ex.what() << std::endl;
            return 1;
        }
    } else {
        fprintf(stderr, "Unknown read mode: '%*.s'\n", int(args.mode.size()), args.mode.c_str());
        return 1;
    }

    if (block_size == 0) {
        fprintf(stderr, "Insufficient block size %zu. Try increasing it!\n", block_size);
        return 1;
    }
    auto block = std::vector<uint8_t>(block_size);

    size_t total_read = 0;
    bool is_reading = true;
    while (is_reading) {
        // check if reaching maximum loop size
        bool should_loop = false;
        if (loop_size.has_value()) {
            size_t remaining_bytes = loop_size.value() - total_read;
            if (block_size > remaining_bytes) {
                should_loop = true;
            }
        }

        // try to read if block fits within remaining file
        if (!should_loop) {
            const size_t nb_read = read_bytes(block);
            total_read += nb_read;
            if (nb_read != block_size) {
                should_loop = true;
            }
        }

        // loop around
        if (should_loop) {
            total_read = 0;
            const bool is_success = loop_file();
            if (!is_success) {
                fprintf(stderr, "Failed to loop file. Exiting early...\n");
                is_reading = false;
                break;
            }
        }

        const size_t nb_write = fwrite(block.data(), sizeof(uint8_t), block.size(), fp_out);
        if (nb_write != block.size()) {
            fprintf(stderr, "Failed to write out block %zu/%zu bytes. Exiting...\n", nb_write, block.size());
            break;
        }
    }
    return 0;
}
