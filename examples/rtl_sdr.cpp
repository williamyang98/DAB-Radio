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

#include <errno.h>
#include <signal.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include <vector>
#include <limits>
#include <algorithm>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#endif

#include "./getopt/getopt.h"
#include "./block_frequencies.h"

extern "C" {
#include <rtl-sdr.h>
}

constexpr int DEFAULT_SAMPLE_RATE = 2048000;
constexpr int DEFAULT_BUF_LENGTH = 16*16384;
constexpr int MINIMAL_BUF_LENGTH = 512;
constexpr int MAXIMAL_BUF_LENGTH = 256*16384;
constexpr int AUTOMATIC_GAIN = 0;

struct GlobalContext {
    bool is_user_exit = false;
    rtlsdr_dev_t *device = NULL;
};

static GlobalContext global_context {};

int read_sync(FILE *file, const uint32_t out_block_size, uint32_t bytes_to_read);
int read_async(FILE *file, const uint32_t out_block_size, uint32_t bytes_to_read);
double atofs(char *s);
double atoft(char *s);
double atofp(char *s);
int find_nearest_gain(rtlsdr_dev_t *dev, int target_gain);
int verbose_set_frequency(rtlsdr_dev_t *dev, uint32_t frequency);
int verbose_set_sample_rate(rtlsdr_dev_t *dev, uint32_t samp_rate);
int verbose_direct_sampling(rtlsdr_dev_t *dev, int on);
int verbose_offset_tuning(rtlsdr_dev_t *dev);
int verbose_auto_gain(rtlsdr_dev_t *dev);
int verbose_gain_set(rtlsdr_dev_t *dev, int gain);
int verbose_ppm_set(rtlsdr_dev_t *dev, int ppm_error);
int verbose_reset_buffer(rtlsdr_dev_t *dev);
int verbose_device_search(const char *search_str);

#if defined(_WIN32)
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

struct Arguments {
    const char *channel = "9C";
    int frequency = 0;
    int samp_rate = DEFAULT_SAMPLE_RATE;
    char *filename = NULL;
    int dev_index = 0;
    bool is_dev_given = false;
    int gain = int(22.9f * 10.0f);
    int ppm_error = 0;
    int out_block_size = DEFAULT_BUF_LENGTH;
    int bytes_to_read = 0;
    bool sync_mode = false;
    int direct_sampling = 0;    // 0: IQ, 1: 1/I, 2: 2/Q 
    bool is_offset_tuning = false;
    bool is_enable_bias_tee = false;
};

void usage(void) {
    Arguments args;

    const char* format_string = 
        "rtl_sdr, an I/Q recorder for RTL2832 based DVB-T receivers\n"
        "Usage: [-c <channel_to_tune_to> (default: %s)]\n"
        "       [-f <frequency_to_tune_to> (default: 206.352MHz @ %s)]\n"
        "       [-s <samplerate> (default: %d Hz)]\n"
        "       [-o <filename> (default: stdout)\n"
        "       [-d <device_index> (default: %d)]\n"
        "       [-g <gain> (default: %.1fdB) (0 for auto)]\n"
        "       [-p <ppm_error> (default: %d)]\n"
        "       [-b <output_block_size> (default: %d)]\n"
        "       [-n <number_of_samples_to_read> (default: %d, infinite)]\n"
        "       [-S force sync output (default: %s)]\n"
        "       [-E enable_option (default: none)]\n"
        "           use multiple -E to enable multiple options\n"
        "           direct:  enable direct sampling 1 (usually I)\n"
        "           direct2: enable direct sampling 2 (usually Q)\n"
        "           offset:  enable offset tuning\n"
        "       [-T enable bias-T on GPIO PIN 0 (works for rtl-sdr.com v3 dongles)]\n"
        "       [-L lists DAB channel]\n"
        "       [-h shows help]\n"
    ;

    fprintf(
        stderr, format_string,
        args.channel,
        args.channel,
        args.samp_rate,
        args.dev_index,
        float(args.gain)/10.0f,
        args.ppm_error,
        args.out_block_size,
        args.bytes_to_read,
        args.sync_mode ? "sync" : "async"
    );
}

void list_channels(void) {
    struct Channel {
        const char *key;
        uint32_t frequency;
        Channel(const char *_key, const uint32_t _frequency): key(_key), frequency(_frequency) {}
    };

    // Sort by frequency
    std::vector<Channel> channels;
    for (const auto& [key, value]: block_frequencies) {
        channels.emplace_back(key.c_str(), value);
    }
    std::sort(channels.begin(), channels.end(), [] (const Channel& a, const Channel& b) {
        return a.frequency < b.frequency;
    });

    fprintf(stderr, "Block |    Frequency\n");
    for (const auto& channel: channels) {
        fprintf(stderr, "%*s | %8.3f MHz\n", 5, channel.key, float(channel.frequency) * 1e-6f);
    }
}

int parse_arguments(Arguments& args, int argc, char **argv) {
    // IQ has 8bits per component
    constexpr int bytes_per_sample = 2;

    while (true) {
        const int opt = getopt_custom(argc, argv, "c:f:s:o:d:g:p:b:n:SE:TLh"); 
        if (opt == -1) {
            break;
        }
        switch (opt) {
        case 'c':
            args.channel = optarg;
            break;
        case 'f':
            args.frequency = int(atofs(optarg));
            args.channel = NULL;
            break;
        case 's':
            args.samp_rate = int(atofs(optarg));
            break;
        case 'o':
            args.filename = optarg;
            break;
        case 'd':
            args.dev_index = verbose_device_search(optarg);
            args.is_dev_given = true;
            break;
        case 'g':
            args.gain = int(atof(optarg) * 10); /* tenths of a dB */
            break;
        case 'p':
            args.ppm_error = atoi(optarg);
            break;
        case 'b':
            args.out_block_size = int(atof(optarg));
            break;
        case 'n':
            args.bytes_to_read = int(atof(optarg)) * bytes_per_sample;
            break;
        case 'S':
            args.sync_mode = true;
            break;
        case 'E':
            if (strncmp(optarg, "direct", 7) == 0) {
                args.direct_sampling = 1;
            } else if (strncmp(optarg, "direct2", 8) == 0) {
                args.direct_sampling = 2;
            } else if (strncmp(optarg, "offset", 7) == 0) {
                args.is_offset_tuning = true;
            } else {
                fprintf(stderr, "Unknown option for -E '%s'\n\n", optarg);
                return 1;
            }
            break;
        case 'T':
            args.is_enable_bias_tee = true;
            break;
        case 'L':
            list_channels();
            return 1;
        case 'h':
        case '?':
        default:
            usage();
            return 1;
        }
    }

    if (args.channel != NULL) {
        auto res = block_frequencies.find(args.channel);
        if (res == block_frequencies.end()) {
            fprintf(stderr, "Invalid channel block '%s'. Refer to -l to list valid blocks.\n", args.channel);
            return 1;
        }
        args.frequency = int(res->second);
    }

    if (args.frequency < 0) {
        fprintf(stderr, "Center frequency must be positive (%d < 0).\n", args.frequency);
        return 1;
    }

    if (args.samp_rate <= 0) {
        fprintf(stderr, "Sampling rate must be positive (%d <= 0).\n", args.samp_rate);
        return 1;
    }

    if (args.bytes_to_read < 0) {
        fprintf(stderr, "Number of bytes to read must be positive (%d < 0).\n", args.bytes_to_read);
        return 1;
    }

    if ((args.out_block_size < MINIMAL_BUF_LENGTH) || (args.out_block_size > MAXIMAL_BUF_LENGTH)) {
        fprintf(stderr, "Output block size wrong value, falling back to default\n");
        fprintf(stderr, "Minimal length: %u\n", MINIMAL_BUF_LENGTH);
        fprintf(stderr, "Maximal length: %u\n", MAXIMAL_BUF_LENGTH);
        args.out_block_size = DEFAULT_BUF_LENGTH;
    }

    if (args.dev_index < 0) {
        fprintf(stderr, "Got a negative device index (%d)\n", args.dev_index);
        return 1;
    }

    return 0;
}

int main(int argc, char **argv) {
    Arguments args; 
    {
        const int res = parse_arguments(args, argc, argv);
        if (res > 0) {
            return res;
        }
    }

    FILE *file = stdout;
    if (args.filename != NULL) {
        file = fopen(args.filename, "wb+");
        if (file == NULL) {
            fprintf(stderr, "Failed to open '%s'\n", args.filename);
            return 1;
        }
    }

    #if defined(_WIN32)
    _setmode(_fileno(file), _O_BINARY);
    #endif

    if (args.channel != NULL) {
        fprintf(stderr, "Selected %s @ %.3f MHz.\n", args.channel, float(args.frequency) * 1e-6f);
    }

    int device_index = args.dev_index;
    if (!args.is_dev_given) {
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
    #if !defined(_WIN32)
    {
        struct sigaction sigact;
        sigact.sa_handler = sighandler;
        sigemptyset(&sigact.sa_mask);
        sigact.sa_flags = 0;
        sigaction(SIGINT, &sigact, NULL);
        sigaction(SIGTERM, &sigact, NULL);
        sigaction(SIGQUIT, &sigact, NULL);
        sigaction(SIGPIPE, &sigact, NULL);
    }
    #else
    SetConsoleCtrlHandler(sighandler, TRUE);
    #endif

    verbose_set_sample_rate(device, uint32_t(args.samp_rate));
    verbose_set_frequency(device, uint32_t(args.frequency));

    rtlsdr_set_bias_tee(device, args.is_enable_bias_tee ? 1 : 0);
    if (args.is_enable_bias_tee) {
        fprintf(stderr, "Activated bias-T on GPIO PIN 0.\n");
    }

    verbose_ppm_set(device, args.ppm_error);
    verbose_direct_sampling(device, args.direct_sampling);
    if (args.is_offset_tuning) {
        verbose_offset_tuning(device);
    }

    if (args.gain == AUTOMATIC_GAIN) {
        verbose_auto_gain(device);
    } else {
        const int nearest_gain = find_nearest_gain(device, args.gain);
        verbose_gain_set(device, nearest_gain);
    }

    verbose_reset_buffer(device);

    int read_result = 0;
    if (args.sync_mode) {
        fprintf(stderr, "Reading samples in sync mode...\n");
        read_result = read_sync(file, uint32_t(args.out_block_size), uint32_t(args.bytes_to_read));
    } else {
        fprintf(stderr, "Reading samples in async mode...\n");
        read_result = read_async(file, uint32_t(args.out_block_size), uint32_t(args.bytes_to_read));
    }

    if (global_context.is_user_exit) {
        fprintf(stderr, "\nUser cancel, exiting...\n");
    } else {
        fprintf(stderr, "\nLibrary error %d, exiting...\n", read_result);
    }

    fclose(file);
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
        if (user_data == NULL) {
            return;
        }

        auto &local_context = *reinterpret_cast<context_t*>(user_data);
        if (local_context.file_out == NULL) {
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

double atofs(char *s) {
    const size_t len = strlen(s);
    const char last = s[len-1];
    s[len-1] = '\0';

    /* standard suffixes */
    double suff = 1.0;
    switch (last) {
        case 'g':
        case 'G':
            suff *= 1e3;
            /* fall-through */
        case 'm':
        case 'M':
            suff *= 1e3;
            /* fall-through */
        case 'k':
        case 'K':
            suff *= 1e3;
            suff *= atof(s);
            s[len-1] = last;
            return suff;
    }
    s[len-1] = last;
    return atof(s);
}

double atoft(char *s) {
    const size_t len = strlen(s);
    const char last = s[len-1];
    s[len-1] = '\0';

    /* time suffixes, returns seconds */
    double suff = 1.0;
    switch (last) {
        case 'h':
        case 'H':
            suff *= 60;
            /* fall-through */
        case 'm':
        case 'M':
            suff *= 60;
            /* fall-through */
        case 's':
        case 'S':
            suff *= atof(s);
            s[len-1] = last;
            return suff;
    }
    s[len-1] = last;
    return atof(s);
}

double atofp(char *s) {
    const size_t len = strlen(s);
    const char last = s[len-1];
    s[len-1] = '\0';

    /* percent suffixes */
    double suff = 1.0;
    switch (last) {
        case '%':
            suff *= 0.01;
            suff *= atof(s);
            s[len-1] = last;
            return suff;
    }
    s[len-1] = last;
    return atof(s);
}

int find_nearest_gain(rtlsdr_dev_t *dev, int target_gain) {
    const int res = rtlsdr_set_tuner_gain_mode(dev, 1);
    if (res < 0) {
        fprintf(stderr, "WARNING: Failed to enable manual gain (%d).\n", res);
        return res;
    }

    const int count = rtlsdr_get_tuner_gains(dev, NULL);
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
    if (search_str == NULL) {
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
        char *s_end = NULL;
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
