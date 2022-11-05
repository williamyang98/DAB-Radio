// Basic radio scraper that includes the DAB digital decoder
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include <io.h>
#include <fcntl.h>

#include "getopt/getopt.h"
#include "easylogging++.h"
#include "dab/logging.h"
#include "basic_radio/basic_radio.h"

#include "basic_scraper/basic_scraper.h"

class AppDependencies: public Basic_Radio_Dependencies 
{
public:
    virtual PCM_Player* Create_PCM_Player(void) {
        return NULL;
    }
};

// Class that connects our analog OFDM demodulator and digital DAB decoder
// Also provides the skeleton of the Imgui application
class App
{
private:
    AppDependencies dependencies;

    int nb_buf_bits;
    viterbi_bit_t* bits_buf;
    FILE* const fp_in;

    BasicRadio* radio;
    BasicScraper* scraper;
public:
    App(const int transmission_mode, FILE* const _fp_in, const char* dir)
    : fp_in(_fp_in)
    {
        auto params = get_dab_parameters(transmission_mode);
        nb_buf_bits = params.nb_frame_bits;
        radio = new BasicRadio(params, &dependencies);
        scraper = new BasicScraper(radio, dir);
        bits_buf = new viterbi_bit_t[nb_buf_bits];
    }
    ~App() {
        delete scraper;
        delete radio;
        delete [] bits_buf;
    }
    void Run() {
        while (true) {
            const auto nb_read = fread(bits_buf, sizeof(viterbi_bit_t), nb_buf_bits, fp_in);
            if (nb_read != nb_buf_bits) {
                fprintf(stderr, "Failed to read soft-decision bits (%llu/%d)\n", nb_read, nb_buf_bits);
                break;
            }
            radio->Process(bits_buf, nb_buf_bits);
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
    char* output_dir = NULL;
    char* rd_filename = NULL;
    bool is_logging = false;
    int transmission_mode = 1;

    int opt; 
    while ((opt = getopt(argc, argv, "o:i:M:vh")) != -1) {
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
        errno_t err = fopen_s(&fp_in, rd_filename, "r");
        if (err != 0) {
            fprintf(stderr, "Failed to open file for reading\n");
            return 1;
        }
    }

    _setmode(_fileno(fp_in), _O_BINARY);

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

    auto app = new App(transmission_mode, fp_in, output_dir);
    app->Run();
    delete app;
    return 0;
}

