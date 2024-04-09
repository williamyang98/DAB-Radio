/*
 * rtl-sdr, turns your Realtek RTL2832 based DVB dongle into a SDR receiver
 * Copyright (C) 2012 by Steve Markgraf <steve@steve-m.de>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <exception>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#if _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#else
#include <signal.h>
#endif

#include <argparse/argparse.hpp>
#include "./block_frequencies.h"

extern "C" {
#include <rtl-sdr.h>
}

struct GlobalContext {
    bool is_user_exit = false;
    rtlsdr_dev_t *device = nullptr;
};

static GlobalContext global_context {};
static int read_sync(FILE *file, const uint32_t out_block_size, uint32_t bytes_to_read);
static int read_async(FILE *file, const uint32_t out_block_size, uint32_t bytes_to_read);
static int find_nearest_gain(rtlsdr_dev_t *dev, int target_gain);
static int verbose_set_frequency(rtlsdr_dev_t *dev, uint32_t frequency);
static int verbose_set_sample_rate(rtlsdr_dev_t *dev, uint32_t samp_rate);
static int verbose_direct_sampling(rtlsdr_dev_t *dev, int on);
static int verbose_offset_tuning(rtlsdr_dev_t *dev);
static int verbose_auto_gain(rtlsdr_dev_t *dev);
static int verbose_gain_set(rtlsdr_dev_t *dev, int gain);
static int verbose_ppm_set(rtlsdr_dev_t *dev, int ppm_error);
static int verbose_reset_buffer(rtlsdr_dev_t *dev);
static int verbose_device_search(const char *search_str);

#if _WIN32
BOOL WINAPI sighandler(DWORD signum) {
    if (signum == CTRL_C_EVENT) {
        fprintf(stderr, "Signal caught, exiting!\n");
        global_context.is_user_exit = true;
        rtlsdr_cancel_async(global_context.device);
        return TRUE;
    }
    return FALSE;
}
#else
void sighandler(int signum) {
    fprintf(stderr, "Signal caught, exiting!\n");
    global_context.is_user_exit = true;
    rtlsdr_cancel_async(global_context.device);
}
#endif

void init_parser(argparse::ArgumentParser& parser) {
    parser.add_argument("-c", "--channel")
        .default_value(std::string(""))
        .metavar("CHANNEL")
        .nargs(1).required()
        .help("DAB channel to tune to (use --list-channels to show valid channel blocks)");
    parser.add_argument("--list-channels")
        .default_value(false).implicit_value(true)
        .help("List all DAB channels");
    parser.add_argument("-f", "--frequency")
        .default_value(float(0.0f)).scan<'g', float>()
        .metavar("FREQUENCY")
        .nargs(1).required()
        .help("Frequency to tune to (defaults to --channel argument if not specified)");
    parser.add_argument("-s", "--sampling-rate")
        .default_value(float(2'048'000)).scan<'g', float>()
        .metavar("SAMPLING_RATE")
        .nargs(1).required()
        .help("Sampling rate of receiver in Hz");
    parser.add_argument("-o", "--output")
        .default_value(std::string(""))
        .metavar("OUTPUT_FILENAME")
        .nargs(1).required()
        .help("Filename of output (defaults to stdout)");
    parser.add_argument("-d", "--device")
        .default_value(int(0)).scan<'i', int>()
        .metavar("INDEX")
        .nargs(1).required()
        .help("Index from all connected devices");
    parser.add_argument("-g", "--gain")
        .default_value(float(19.0f)).scan<'g', float>()
        .metavar("GAIN")
        .nargs(1).required()
        .help("Gain of receiver in dB");
    parser.add_argument("--auto-gain")
        .default_value(false).implicit_value(true)
        .help("Enable automatic gain");
    parser.add_argument("-p", "--ppm")
        .default_value(int(0)).scan<'i', int>()
        .metavar("PPM")
        .nargs(1).required()
        .help("Parts per million of center frequency to adjust tuning frequency");
    parser.add_argument("-b", "--block-size")
        .default_value(size_t(65536)).scan<'u', size_t>()
        .metavar("BLOCK_SIZE")
        .nargs(1).required()
        .help("Number of bytes to read in a block from device");
    parser.add_argument("-n", "--total-bytes")
        .default_value(size_t(0)).scan<'u', size_t>()
        .metavar("TOTAL_BYTES")
        .nargs(1).required()
        .help("Number of bytes to read from receiver (0 defaults to continuous reading)");
    parser.add_argument("--sync")
        .default_value(false).implicit_value(true)
        .help("Read samples in the main thread synchronously instead of asynchronously through a callback");
    parser.add_argument("--sampling-mode")
        .default_value(std::string("iq"))
        .choices("iq", "direct_i", "direct_q")
        .metavar("MODE")
        .nargs(1).required()
        .help("Use direct sampling to receive lower frequencies");
    parser.add_argument("--offset-tuning")
        .default_value(false).implicit_value(true)
        .help("Enable offset tuning");
    parser.add_argument("--enable-bias-tee")
        .default_value(false).implicit_value(true)
        .help("Enable bias-T which supplies DC voltage usually to an active antenna");
}

enum class SamplingMode: uint32_t {
    IQ=0, 
    DIRECT_I=1, 
    DIRECT_Q=2,
};

struct Args {
    std::optional<std::string> channel;
    bool is_list_channels;
    std::optional<float> frequency_Hz;
    float sampling_rate;
    std::string output_filename;
    std::optional<int> device_index;
    float manual_gain;
    bool is_automatic_gain;
    int ppm;
    size_t block_size;
    size_t bytes_to_read;
    bool is_sync;
    SamplingMode sampling_mode;
    bool is_offset_tuning;
    bool is_enable_bias_tee;
};

Args get_args_from_parser(const argparse::ArgumentParser& parser) {
    Args args;
    const std::string channel = parser.get<std::string>("--channel");
    args.channel = std::nullopt;
    if (parser.is_used("--channel")) {
        args.channel = std::optional(channel);
    }
    args.is_list_channels = parser.get<bool>("--list-channels");
    const float frequency_Hz = parser.get<float>("--frequency");
    args.frequency_Hz = std::nullopt;
    if (parser.is_used("--frequency")) {
        args.frequency_Hz = std::optional(frequency_Hz);
    }
    args.sampling_rate = parser.get<float>("--sampling-rate");
    args.output_filename = parser.get<std::string>("--output"); 
    const int device_index = parser.get<int>("--device");
    args.device_index = std::nullopt;
    if (parser.is_used("--device")) {
        args.device_index = std::optional(device_index);
    } 
    args.manual_gain = parser.get<float>("--gain");
    args.is_automatic_gain = parser.get<bool>("--auto-gain");
    args.ppm = parser.get<int>("--ppm");
    args.block_size = parser.get<size_t>("--block-size");
    args.bytes_to_read = parser.get<size_t>("--total-bytes");
    args.is_sync = parser.get<bool>("--sync");
    const auto sampling_mode = parser.get<std::string>("--sampling-mode");
    args.sampling_mode = SamplingMode::IQ;
    if (sampling_mode.compare("direct_i") == 0) {
        args.sampling_mode = SamplingMode::DIRECT_I;
    } else if (sampling_mode.compare("direct_q") == 0) {
        args.sampling_mode = SamplingMode::DIRECT_Q;
    }
    args.is_offset_tuning = parser.get<bool>("--offset-tuning");
    args.is_enable_bias_tee = parser.get<bool>("--enable-bias-tee");
    return args;
}

static void list_channels() {
    struct Channel {
        const char *name;
        uint32_t frequency_Hz;
    };
    // Sort by frequency
    std::vector<Channel> channels;
    for (const auto& [channel, frequency_Hz]: block_frequencies) {
        channels.push_back({ channel.c_str(), frequency_Hz });
    }
    std::sort(channels.begin(), channels.end(), [](const auto& a, const auto& b) {
        return a.frequency_Hz < b.frequency_Hz;
    });
    fprintf(stderr, "Block |    Frequency\n");
    for (const auto& channel: channels) {
        const float frequency_MHz = float(channel.frequency_Hz) * 1e-6f;
        fprintf(stderr, "%*s | %8.3f MHz\n", 5, channel.name, frequency_MHz);
    }
}

int main(int argc, char **argv) {
    auto parser = argparse::ArgumentParser("rtl_sdr", "0.1.0");
    parser.add_description("An I/Q recorder for RTL2832 based DVB-T receivers");
    init_parser(parser);
    try {
        parser.parse_args(argc, argv);
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << std::endl;
        std::cerr << parser;
        return 1;
    }
    const auto args = get_args_from_parser(parser);
    if (args.is_list_channels) {
        fprintf(stderr, "Valid DAB channels are:\n");
        list_channels();
        return 1;
    }
    if (args.block_size == 0) {
        fprintf(stderr, "Block size cannot be zero\n");
        return 1;
    }
    if (args.sampling_rate <= 0.0f) {
        fprintf(stderr, "Sampling rate must be positive (%.3f)\n", args.sampling_rate);
        return 1;
    }
    // get center frequency
    uint32_t frequency_Hz = 0;
    if (args.channel.has_value()) {
        const auto channel = args.channel.value();
        auto res = block_frequencies.find(channel); 
        if (res == block_frequencies.end()) {
            fprintf(stderr, "Invalid channel block '%s'. Refer to --list-channels for valid blocks\n", channel.c_str());
            list_channels();
            return 1;
        }
        frequency_Hz = res->second;
        fprintf(stderr, "Selected channel %s @ %.3f MHz.\n", channel.c_str(), float(frequency_Hz) * 1e-6f);
    } else if (args.frequency_Hz.has_value()) {
        const float frequency = args.frequency_Hz.value();
        if (frequency <= 0.0f) {
            fprintf(stderr, "Frequency must be positive (%.3f)\n", frequency);
            return 1;
        }
        frequency_Hz = uint32_t(frequency);
        fprintf(stderr, "Selected manual frequency at %.3f MHz.\n", float(frequency_Hz) * 1e-6f);
    } else {
        fprintf(stderr, "Must specify either a channel block or specific frequency value to tune to. Set --channel or --frequency.\n");
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
    _setmode(_fileno(fp_out), _O_BINARY);
#endif

    int device_index = 0;
    if (args.device_index.has_value()) {
        device_index = args.device_index.value();
    } else {
        device_index = verbose_device_search("0");
    }

    if (device_index < 0) {
        fprintf(stderr, "Got a negative device index (%d)\n", device_index);
        return 1;
    }

    auto& device = global_context.device;
    {
        const int res = rtlsdr_open(&device, uint32_t(device_index));
        if (res < 0) {
            fprintf(stderr, "Failed to open rtlsdr device #%d (%d).\n", device_index, res);
            return 1;
        }
    }

    // NOTE: Set sig handler after device is open for cleanup
#if !_WIN32
    {
        struct sigaction sigact;
        sigact.sa_handler = sighandler;
        sigemptyset(&sigact.sa_mask);
        sigact.sa_flags = 0;
        sigaction(SIGINT, &sigact, nullptr);
        sigaction(SIGTERM, &sigact, nullptr);
        sigaction(SIGQUIT, &sigact, nullptr);
        sigaction(SIGPIPE, &sigact, nullptr);
    }
#else
    SetConsoleCtrlHandler(sighandler, TRUE);
#endif

    verbose_set_sample_rate(device, uint32_t(args.sampling_rate));
    verbose_set_frequency(device, frequency_Hz);

    rtlsdr_set_bias_tee(device, args.is_enable_bias_tee ? 1 : 0);
    if (args.is_enable_bias_tee) {
        fprintf(stderr, "Activated bias-T on GPIO PIN 0.\n");
    }

    verbose_ppm_set(device, args.ppm);
    verbose_direct_sampling(device, static_cast<uint32_t>(args.sampling_mode));
    if (args.is_offset_tuning) {
        verbose_offset_tuning(device);
    }

    if (args.is_automatic_gain) {
        verbose_auto_gain(device);
    } else {
        const int user_gain = int(args.manual_gain * 10.0f);
        const int nearest_gain = find_nearest_gain(device, user_gain);
        verbose_gain_set(device, nearest_gain);
    }

    verbose_reset_buffer(device);

    int read_result = 0;
    if (args.is_sync) {
        fprintf(stderr, "Reading samples in sync mode...\n");
        read_result = read_sync(fp_out, uint32_t(args.block_size), uint32_t(args.bytes_to_read));
    } else {
        fprintf(stderr, "Reading samples in async mode...\n");
        read_result = read_async(fp_out, uint32_t(args.block_size), uint32_t(args.bytes_to_read));
    }

    if (global_context.is_user_exit) {
        fprintf(stderr, "\nUser cancel, exiting...\n");
    } else {
        fprintf(stderr, "\nLibrary error %d, exiting...\n", read_result);
    }

    fclose(fp_out);
    rtlsdr_close(global_context.device);
    return (read_result >= 0) ? read_result : -read_result;
}

int read_sync(FILE *file, const uint32_t out_block_size, uint32_t bytes_to_read) {
    std::vector<uint8_t> buffer(out_block_size);

    while (!global_context.is_user_exit) {
        int n_read = 0;
        const int res = rtlsdr_read_sync(global_context.device, buffer.data(), out_block_size, &n_read);
        if (res < 0) {
            fprintf(stderr, "WARNING: sync read failed (%d).\n", res);
            return res;
        }

        if ((bytes_to_read > 0) && (uint32_t(n_read) > bytes_to_read)) {
            n_read = int(bytes_to_read);
            global_context.is_user_exit = true;
        }

        if (fwrite(buffer.data(), 1, n_read, file) != size_t(n_read)) {
            fprintf(stderr, "Short write, samples lost, exiting!\n");
            break;
        }

        if (uint32_t(n_read) < out_block_size) {
            fprintf(stderr, "Short read, samples lost, exiting!\n");
            break;
        }

        if (bytes_to_read > 0) {
            bytes_to_read -= uint32_t(n_read);
        }
    }

    return 0;
}

int read_async(FILE *file, const uint32_t out_block_size, uint32_t bytes_to_read) {
    struct context_t {
        uint32_t bytes_to_read;
        FILE *file_out;
    } context;

    context.bytes_to_read = bytes_to_read;
    context.file_out = file;

    auto rtlsdr_callback = [](unsigned char *buf, uint32_t len, void *user_data) {
        if (user_data == nullptr) {
            return;
        }

        auto &local_context = *reinterpret_cast<context_t*>(user_data);
        if (local_context.file_out == nullptr) {
            return;
        }

        if (global_context.is_user_exit) {
            return;
        }

        if ((local_context.bytes_to_read > 0) && (len > local_context.bytes_to_read)) {
            len = local_context.bytes_to_read;
            global_context.is_user_exit = true;
            rtlsdr_cancel_async(global_context.device);
        }

        if (fwrite(buf, 1, len, local_context.file_out) != len) {
            fprintf(stderr, "Short write, samples lost, exiting!\n");
            global_context.is_user_exit = true;
            rtlsdr_cancel_async(global_context.device);
            return;
        }

        if (local_context.bytes_to_read > 0) {
            local_context.bytes_to_read -= len;
        }
    };

    const int res = rtlsdr_read_async(global_context.device, rtlsdr_callback, reinterpret_cast<void *>(&context), 0, out_block_size);
    return res;
}

int find_nearest_gain(rtlsdr_dev_t *dev, int target_gain) {
    const int res = rtlsdr_set_tuner_gain_mode(dev, 1);
    if (res < 0) {
        fprintf(stderr, "WARNING: Failed to enable manual gain (%d).\n", res);
        return res;
    }

    const int count = rtlsdr_get_tuner_gains(dev, nullptr);
    if (count <= 0) {
        return 0;
    }

    std::vector<int> gains(count);
    rtlsdr_get_tuner_gains(dev, gains.data());

    int nearest_gain = gains[0];
    int minimum_error = std::numeric_limits<int>::max();
    for (int i = 0; i < count; i++) {
        const int error = abs(target_gain - gains[i]);
        if (error < minimum_error) {
            minimum_error = error;
            nearest_gain = gains[i];
        }
    }
    return nearest_gain;
}

int verbose_set_frequency(rtlsdr_dev_t *dev, uint32_t frequency) {
    const int res = rtlsdr_set_center_freq(dev, frequency);
    if (res < 0) {
        fprintf(stderr, "WARNING: Failed to set center freq (%d).\n", res);
    } else {
        fprintf(stderr, "Tuned to %u Hz.\n", frequency);
    }
    return res;
}

int verbose_set_sample_rate(rtlsdr_dev_t *dev, uint32_t samp_rate) {
    const int r = rtlsdr_set_sample_rate(dev, samp_rate);
    if (r < 0) {
        fprintf(stderr, "WARNING: Failed to set sample rate (%d).\n", r);
    } else {
        fprintf(stderr, "Sampling at %u S/s.\n", samp_rate);
    }
    return r;
}

int verbose_direct_sampling(rtlsdr_dev_t *dev, int on) {
    const int r = rtlsdr_set_direct_sampling(dev, on);
    if (r != 0) {
        fprintf(stderr, "WARNING: Failed to set direct sampling mode (%d).\n", r);
        return r;
    }

    switch (on) {
    case 0:  fprintf(stderr, "Direct sampling mode disabled.\n"); break;
    case 1:  fprintf(stderr, "Enabled direct sampling mode, input 1/I.\n"); break;
    case 2:  fprintf(stderr, "Enabled direct sampling mode, input 2/Q.\n"); break;
    default: fprintf(stderr, "Unknown sampling mode (%d).\n", on); break;
    }

    return r;
}

int verbose_offset_tuning(rtlsdr_dev_t *dev) {
    const int r = rtlsdr_set_offset_tuning(dev, 1);
    if (r != 0) {
        fprintf(stderr, "WARNING: Failed to set offset tuning (%d).\n", r);
    } else {
        fprintf(stderr, "Offset tuning mode enabled.\n");
    }
    return r;
}

int verbose_auto_gain(rtlsdr_dev_t *dev) {
    const int r = rtlsdr_set_tuner_gain_mode(dev, 0);
    if (r != 0) {
        fprintf(stderr, "WARNING: Failed to set tuner gain (%d).\n", r);
    } else {
        fprintf(stderr, "Tuner gain set to automatic.\n");
    }
    return r;
}

int verbose_gain_set(rtlsdr_dev_t *dev, int gain) {
    int r;
    r = rtlsdr_set_tuner_gain_mode(dev, 1);
    if (r < 0) {
        fprintf(stderr, "WARNING: Failed to enable manual gain (%d).\n", r);
        return r;
    }

    r = rtlsdr_set_tuner_gain(dev, gain);
    if (r != 0) {
        fprintf(stderr, "WARNING: Failed to set tuner gain (%d).\n", r);
    } else {
        fprintf(stderr, "Tuner gain set to %0.2f dB.\n", gain/10.0);
    }
    return r;
}

int verbose_ppm_set(rtlsdr_dev_t *dev, int ppm_error) {
    if (ppm_error == 0) {
        return 0;
    }
    const int r = rtlsdr_set_freq_correction(dev, ppm_error);
    if (r < 0) {
        fprintf(stderr, "WARNING: Failed to set ppm error (%d).\n", r);
    } else {
        fprintf(stderr, "Tuner error set to %i ppm.\n", ppm_error);
    }
    return r;
}

int verbose_reset_buffer(rtlsdr_dev_t *dev) {
    const int r = rtlsdr_reset_buffer(dev);
    if (r < 0) {
        fprintf(stderr, "WARNING: Failed to reset buffers (%d).\n", r);
    }
    return r;
}

int verbose_device_search(const char *search_str) {
    if (search_str == nullptr) {
        fprintf(stderr, "No device string provided.\n");
        return -1;
    }
    const size_t search_str_length = strlen(search_str);

    const int device_count = rtlsdr_get_device_count();
    if (!device_count) {
        fprintf(stderr, "No supported devices found.\n");
        return -1;
    }

    constexpr int MAX_STR_LENGTH = 256;
    char vendor_str[MAX_STR_LENGTH]; 
    char product_str[MAX_STR_LENGTH]; 
    char serial_str[MAX_STR_LENGTH];

    fprintf(stderr, "Found %d device(s):\n", device_count);
    for (int i = 0; i < device_count; i++) {
        rtlsdr_get_device_usb_strings(i, vendor_str, product_str, serial_str);
        fprintf(stderr, "  %d:  %s, %s, SN: %s\n", i, vendor_str, product_str, serial_str);
    }
    fprintf(stderr, "\n");

    // does string look like raw id number
    {
        char *s_end = nullptr;
        const int device = int(strtol(search_str, &s_end, 0));
        if ((s_end[0] == '\0') && (device >= 0) && (device < device_count)) {
            fprintf(stderr, "Using device %d: %s\n", device, rtlsdr_get_device_name(uint32_t(device)));
            return device;
        }
    }

    // does string exact match a serial
    for (int i = 0; i < device_count; i++) {
        rtlsdr_get_device_usb_strings(i, vendor_str, product_str, serial_str);
        if (strncmp(search_str, serial_str, MAX_STR_LENGTH) != 0) {
            continue;
        }
        const int device = i;
        fprintf(stderr, "Using device %d: %s\n", device, rtlsdr_get_device_name(uint32_t(device)));
        return device;
    }

    // does string prefix match a serial
    for (int i = 0; i < device_count; i++) {
        rtlsdr_get_device_usb_strings(i, vendor_str, product_str, serial_str);
        if (strncmp(search_str, serial_str, search_str_length) != 0) {
            continue;
        }
        const int device = i;
        fprintf(stderr, "Using device %d: %s\n", device, rtlsdr_get_device_name(uint32_t(device)));
        return device;
    }

    // does string suffix match a serial
    for (int i = 0; i < device_count; i++) {
        rtlsdr_get_device_usb_strings(i, vendor_str, product_str, serial_str);
        const size_t serial_str_length = strnlen(serial_str, MAX_STR_LENGTH);
        const int offset = int(serial_str_length) - int(search_str_length);
        if (offset < 0) {
            continue;
        }
        if (strncmp(search_str, serial_str+offset, search_str_length) != 0) {
            continue;
        }
        const int device = i;
        fprintf(stderr, "Using device %d: %s\n", device, rtlsdr_get_device_name(uint32_t(device)));
        return device;
    }

    fprintf(stderr, "No matching devices found.\n");
    return -1;
}
