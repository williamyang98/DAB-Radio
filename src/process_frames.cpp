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
    const int nb_buf_bytes = 75*1536*2/8;
    uint8_t* buf;
    FILE* const fp_in;

    bool is_running;
    BasicRadio* radio;
    std::thread* radio_thread;
public:
    App(FILE* const _fp_in)
    : fp_in(_fp_in) 
    {
        radio = new BasicRadio();
        buf = new uint8_t[nb_buf_bytes];
        is_running = true;
        radio_thread = new std::thread([this]() {
            RunnerThread();
        });
    }
    ~App() {
        delete radio;
        delete [] buf;
        is_running = false;
        radio_thread->join();
        delete radio_thread;
    }
public:
    virtual GLFWwindow* Create_GLFW_Window(void) {
        return glfwCreateWindow(
            1280, 720, 
            "Basic DAB Radio", 
            NULL, NULL);
    }
    virtual void AfterImguiContextInit() {
        ImguiSkeleton::AfterImguiContextInit();
        auto& io = ImGui::GetIO();
        io.IniFilename =  "imgui_process_frames.ini";
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
            const auto nb_read = fread(buf, sizeof(uint8_t), nb_buf_bytes, fp_in);
            if (nb_read != nb_buf_bytes) {
                fprintf(stderr, "Failed to read %d bytes\n", nb_buf_bytes);
                break;
            }
            radio->ProcessFrame(buf, nb_buf_bytes);
        }
    }    
};

void usage() {
    fprintf(stderr, 
        "process_frames, decoded DAB frame data\n\n"
        "\t[-i input filename (default: None)]\n"
        "\t    If no file is provided then stdin is used\n"
        "\t[-h (show usage)]\n"
    );
}

INITIALIZE_EASYLOGGINGPP
int main(int argc, char** argv) {
    char* rd_filename = NULL;

    int opt; 
    while ((opt = getopt(argc, argv, "i:h")) != -1) {
        switch (opt) {
        case 'i':
            rd_filename = optarg;
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
    defaultConf.setToDefault();
    defaultConf.set(el::Level::Error,   el::ConfigurationType::Enabled, "true");
    defaultConf.set(el::Level::Warning, el::ConfigurationType::Enabled, "true");
    defaultConf.set(el::Level::Info,    el::ConfigurationType::Enabled, "true");
    defaultConf.set(el::Level::Debug,   el::ConfigurationType::Enabled, "true");
    el::Loggers::reconfigureAllLoggers(defaultConf);

    auto app = new App(fp_in);
    const int rv = RenderImguiSkeleton(app);
    delete app;
    return rv;
}

