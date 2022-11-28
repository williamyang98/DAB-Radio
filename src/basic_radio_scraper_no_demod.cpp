// Basic radio scraper that includes the DAB digital decoder
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif

#include "modules/basic_radio/basic_radio.h"
#include "modules/basic_scraper/basic_scraper.h"

#include <memory>
#include <vector>

#include "utility/getopt/getopt.h"
#include "easylogging++.h"
#include "modules/dab/logging.h"

class App
{
private:
    FILE* const fp_in;

    std::vector<viterbi_bit_t> frame_bits;
    std::unique_ptr<BasicRadio> radio;
    std::unique_ptr<BasicScraper> scraper;
public:
    App(const int transmission_mode, FILE* const _fp_in, const char* dir)
    : fp_in(_fp_in)
    {
        auto params = get_dab_parameters(transmission_mode);
        frame_bits.resize(params.nb_frame_bits);
        radio = std::make_unique<BasicRadio>(params);
        scraper = std::make_unique<BasicScraper>(*(radio.get()), dir);
    }
    void Run() {
        while (true) {
            const size_t N = frame_bits.size();
            const size_t nb_read = fread(frame_bits.data(), sizeof(viterbi_bit_t), N, fp_in);
            if (nb_read != N) {
                fprintf(stderr, "Failed to read soft-decision bits (%zu/%zu)\n", nb_read, N);
                break;
            }
            radio->Process(frame_bits);
        }
    }
};

void usage() {
    fprintf(stderr, 
        "basic_radio_scraper_no_demod, Decodes soft decision frame bits and saves DAB channel data\n\n"
        "\t[-o output directory (default: scraper_out)]\n"
        "\t[-i input filename (default: None)]\n"
        "\t    If no file is provided then stdin is used\n"
        "\t[-v Enable logging (default: false)]\n"
        "\t[-M dab transmission mode (default: 1)]\n"
        "\t[-h (show usage)]\n"
    );
}

INITIALIZE_EASYLOGGINGPP
int main(int argc, char** argv) {
    const char* output_dir = NULL;
    const char* rd_filename = NULL;
    bool is_logging = false;
    int transmission_mode = 1;

    int opt; 
    while ((opt = getopt_custom(argc, argv, "o:i:M:vh")) != -1) {
        switch (opt) {
        case 'o':
            output_dir = optarg;
            break;
        case 'i':
            rd_filename = optarg;
            break;
        case 'M':
            transmission_mode = (int)(atof(optarg));
            break;
        case 'v':
            is_logging = true;
            break;
        case 'h':
        default:
            usage();
            return 0;
        }
    }
    
    if (transmission_mode <= 0 || transmission_mode > 4) {
        fprintf(stderr, "Transmission modes: I,II,III,IV are supported not (%d)\n", transmission_mode);
        return 1;
    }

    if (output_dir == NULL) {
        output_dir = "scraper_out";
    }

    // app startup
    FILE* fp_in = stdin;
    if (rd_filename != NULL) {
        fp_in = fopen(rd_filename, "rb");
        if (fp_in == NULL) {
            fprintf(stderr, "Failed to open file for reading\n");
            return 1;
        }
    }

#ifdef _WIN32
    _setmode(_fileno(fp_in), _O_BINARY);
#endif

    auto dab_loggers = RegisterLogging();
    auto basic_radio_logger = el::Loggers::getLogger("basic-radio");
    auto basic_scraper_logger = el::Loggers::getLogger("basic-scraper");

    el::Configurations defaultConf;
    const char* logging_level = is_logging ? "true" : "false";
    defaultConf.setToDefault();
    defaultConf.setGlobally(el::ConfigurationType::Enabled, logging_level);
    defaultConf.setGlobally(el::ConfigurationType::Format, "[%level] [%thread] [%logger] %msg");
    el::Loggers::reconfigureAllLoggers(defaultConf);
    el::Helpers::setThreadName("main-thread");

    el::Configurations scraper_conf; 
    scraper_conf.setToDefault();
    scraper_conf.setGlobally(el::ConfigurationType::Enabled, "true");
    scraper_conf.setGlobally(el::ConfigurationType::Format, "[%level] [%thread] [%logger] %msg");
    basic_scraper_logger->configure(scraper_conf);

    auto app = App(transmission_mode, fp_in, output_dir);
    app.Run();
    return 0;
}

