// Basic radio without the OFDM demodulator
// Only has the DAB digital decoder

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <thread>

#include <io.h>
#include <fcntl.h>

#include "getopt/getopt.h"
#include "easylogging++.h"

#include "dab/logging.h"
#include "basic_radio.h"

#include "gui/render_basic_radio.h"
#include "gui/imgui_skeleton.h"
#include "gui/font_awesome_definitions.h"

#include <GLFW/glfw3.h> 
#include "imgui.h"

class App: public ImguiSkeleton 
{
private:
    // Number of bytes per OFDM frame in transmission mode I
    // NOTE: we are hard coding this because all other transmission modes have been deprecated
    const int nb_buf_bits = 75*1536*2;
    viterbi_bit_t* bits_buf;
    FILE* const fp_in;

    bool is_running;
    BasicRadio* radio;
    std::thread* radio_thread;
public:
    App(FILE* const _fp_in)
    : fp_in(_fp_in) 
    {
        const int transmission_mode = 1;
        radio = new BasicRadio(get_dab_parameters(transmission_mode));
        bits_buf = new viterbi_bit_t[nb_buf_bits];
        is_running = true;
        radio_thread = new std::thread([this]() {
            RunnerThread();
        });
    }
    ~App() {
        is_running = false;
        radio_thread->join();
        delete [] bits_buf;
        delete radio;
        delete radio_thread;
    }
public:
    virtual GLFWwindow* Create_GLFW_Window(void) {
        return glfwCreateWindow(
            1280, 720, 
            "Basic DAB Radio (No OFDM demodulator)", 
            NULL, NULL);
    }
    virtual void AfterImguiContextInit() {
        ImguiSkeleton::AfterImguiContextInit();
        auto& io = ImGui::GetIO();
        io.IniFilename =  "imgui_basic_radio_no_demod.ini";
        io.Fonts->AddFontFromFileTTF("res/Roboto-Regular.ttf", 15.0f);
        {
            static const ImWchar icons_ranges[] = { ICON_MIN_FA, ICON_MAX_FA };
            ImFontConfig icons_config;
            icons_config.MergeMode = true;
            icons_config.PixelSnapH = true;
            io.Fonts->AddFontFromFileTTF("res/font_awesome.ttf", 16.0f, &icons_config, icons_ranges);
        }
        ImGuiSetupCustomConfig();
    }
    virtual void Render() {
        RenderBasicRadio(radio);
    }
private:
    void RunnerThread() {
        while (is_running) {
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
        "basic_radio_app_no_demod, decodes logical OFDM frame as a DAB transmission into a basic radio\n\n"
        "\t[-i input filename (default: None)]\n"
        "\t    If no file is provided then stdin is used\n"
        "\t[-v Enable logging (default: false)]\n"
        "\t[-h (show usage)]\n"
    );
}

INITIALIZE_EASYLOGGINGPP
int main(int argc, char** argv) {
    char* rd_filename = NULL;
    bool is_logging = false;

    int opt; 
    while ((opt = getopt(argc, argv, "i:vh")) != -1) {
        switch (opt) {
        case 'i':
            rd_filename = optarg;
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

    el::Configurations defaultConf;
    const char* logging_level = is_logging ? "true" : "false";
    defaultConf.setToDefault();
    defaultConf.set(el::Level::Error,   el::ConfigurationType::Enabled, logging_level);
    defaultConf.set(el::Level::Warning, el::ConfigurationType::Enabled, logging_level);
    defaultConf.set(el::Level::Info,    el::ConfigurationType::Enabled, logging_level);
    defaultConf.set(el::Level::Debug,   el::ConfigurationType::Enabled, logging_level);
    el::Loggers::reconfigureAllLoggers(defaultConf);

    auto app = new App(fp_in);
    const int rv = RenderImguiSkeleton(app);
    delete app;
    return rv;
}

