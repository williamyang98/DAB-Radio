// Basic radio scraper that includes the OFDM demodulator and the DAB digital decoder
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <complex>

#include <io.h>
#include <fcntl.h>

#include "getopt/getopt.h"
#include "easylogging++.h"
#include "dab/logging.h"

#include "ofdm/ofdm_demodulator.h"
#include "ofdm/dab_ofdm_params_ref.h"
#include "ofdm/dab_prs_ref.h"
#include "ofdm/dab_mapper_ref.h"
#include "basic_radio/basic_radio.h"

#include <thread>
#include "double_buffer.h"

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

    // Number of bytes per OFDM frame in transmission mode I
    // NOTE: we are hard coding this because all other transmission modes have been deprecated
    FILE* fp_in;
    const int block_size;
    std::complex<uint8_t>* rd_in_raw;
    std::complex<float>* rd_in_float;

    // Double buffer our demodulator frame data to radio thread
    DoubleBuffer<viterbi_bit_t>* double_buffer;

    // Blocks that make our radio
    OFDM_Demod* ofdm_demod;
    BasicRadio* radio;
    BasicScraper* scraper;

    // Separate threads for the radio, and raw IQ to OFDM frame demodulator
    std::thread* basic_radio_thread;
public:
    App(const int transmission_mode, FILE* const _fp_in, const int _block_size, const char* dir)
    : fp_in(_fp_in), block_size(_block_size)
    {
        radio = new BasicRadio(get_dab_parameters(transmission_mode), &dependencies);
        scraper = new BasicScraper(radio, dir);
        Init_OFDM_Demodulator(transmission_mode);

        // Buffer to read raw IQ 8bit values and convert to floating point
        rd_in_raw = new std::complex<uint8_t>[block_size];
        rd_in_float = new std::complex<float>[block_size];

        // Create our runner threads
        basic_radio_thread = NULL;
    }
    ~App() {
        double_buffer->Close();
        if (basic_radio_thread != NULL) {
            basic_radio_thread->join();
            delete basic_radio_thread;
        }
        delete scraper;
        delete radio;
        delete ofdm_demod;
        delete [] rd_in_raw;
        delete [] rd_in_float;
        delete double_buffer;
    }
    void Run() {
        basic_radio_thread = new std::thread([this]() {
            RunnerThread_Radio();
        });
        RunnerThread_OFDM_Demod();
    }
private:
    // Get our OFDM demodulator and frequency deinterleaver 
    void Init_OFDM_Demodulator(const int transmission_mode) {
        const OFDM_Params ofdm_params = get_DAB_OFDM_params(transmission_mode);
        auto ofdm_prs_ref = new std::complex<float>[ofdm_params.nb_fft];
        get_DAB_PRS_reference(transmission_mode, ofdm_prs_ref, ofdm_params.nb_fft);
        auto ofdm_mapper_ref = new int[ofdm_params.nb_data_carriers];
        get_DAB_mapper_ref(ofdm_mapper_ref, ofdm_params.nb_data_carriers, ofdm_params.nb_fft);
        ofdm_demod = new OFDM_Demod(ofdm_params, ofdm_prs_ref, ofdm_mapper_ref);

        const int nb_frame_bits = ofdm_demod->Get_OFDM_Frame_Total_Bits();
        double_buffer = new DoubleBuffer<viterbi_bit_t>(nb_frame_bits);
        {
            using namespace std::placeholders;
            ofdm_demod->On_OFDM_Frame().Attach(std::bind(&App::OnOFDMFrame, this, _1, _2, _3));
        } 
        {
            auto& cfg = ofdm_demod->GetConfig();
            cfg.toggle_flags.is_update_data_sym_mag = true;
            cfg.toggle_flags.is_update_tii_sym_mag = true;
        }

        delete [] ofdm_prs_ref;
        delete [] ofdm_mapper_ref;
    }
private:
    // ofdm thread -> ofdm frame callback -> double buffer -> dab thread
    void RunnerThread_OFDM_Demod() {
        while (true) {
            // Read raw 8bit IQ values and convert them to floating point
            if (fp_in == NULL) return;
            const auto nb_read = fread((void*)rd_in_raw, sizeof(std::complex<uint8_t>), block_size, fp_in);
            if (nb_read != block_size) {
                fprintf(stderr, "Failed to read in %d bytes, got %llu bytes\n", 
                    block_size, nb_read);
                break;
            }

            for (int i = 0; i < block_size; i++) {
                auto& v = rd_in_raw[i];
                const float I = static_cast<float>(v.real()) - 127.5f;
                const float Q = static_cast<float>(v.imag()) - 127.5f;
                rd_in_float[i] = std::complex<float>(I, Q);
            }

            ofdm_demod->Process(rd_in_float, block_size);
        }
    }    
    void OnOFDMFrame(const viterbi_bit_t* buf, const int nb_carriers, const int nb_symbols) {
        auto* inactive_frame_buffer = double_buffer->AcquireInactiveBuffer();
        const int nb_frame_bits = double_buffer->GetLength();
        if (inactive_frame_buffer == NULL) {
            return;
        }

        for (int i = 0; i < nb_frame_bits; i++) {
            inactive_frame_buffer[i] = buf[i];
        }
        double_buffer->ReleaseInactiveBuffer();
    }
    void RunnerThread_Radio() {
        while (true) {
            auto* active_frame_buffer = double_buffer->AcquireActiveBuffer();
            const int nb_frame_bits = double_buffer->GetLength();
            if (active_frame_buffer == NULL) {
                return;
            }
            radio->Process(active_frame_buffer, nb_frame_bits);
            double_buffer->ReleaseActiveBuffer();
        }
    }
};

void usage() {
    fprintf(stderr, 
        "basic_radio_scraper, Demodulates signal and saves DAB channel data\n\n"
        "\t[-o output directory (default: scraper_out)]\n"
        "\t[-i input filename (default: None)]\n"
        "\t    If no file is provided then stdin is used\n"
        "\t[-v Enable logging (default: false)]\n"
        "\t[-b block size (default: 8192)]\n"
        "\t[-M dab transmission mode (default: 1)]\n"
        "\t[-h (show usage)]\n"
    );
}

INITIALIZE_EASYLOGGINGPP
int main(int argc, char** argv) {
    char* output_dir = NULL;
    char* rd_filename = NULL;
    int block_size = 8192;
    bool is_logging = false;
    int transmission_mode = 1;

    int opt; 
    while ((opt = getopt(argc, argv, "o:i:b:M:vh")) != -1) {
        switch (opt) {
        case 'o':
            output_dir = optarg;
            break;
        case 'i':
            rd_filename = optarg;
            break;
        case 'b':
            block_size = (int)(atof(optarg));
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
    
    if (block_size <= 0) {
        fprintf(stderr, "Block size must be positive (%d)\n", block_size); 
        return 1;

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
    _setmode(_fileno(stdout), _O_BINARY);

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

    auto app = new App(transmission_mode, fp_in, block_size, output_dir);
    app->Run();
    delete app;
    return 0;
}

