// Reads in raw IQ values from rtl_sdr and converts it into a digital OFDM frame

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <thread>

#include <io.h>
#include <fcntl.h>

#include "./getopt/getopt.h"

#include <complex>
#include "ofdm/ofdm_demodulator.h"

#include "dab_ofdm_params_ref.h"
#include "dab_prs_ref.h"
#include "dab_mapper_ref.h"

#include <GLFW/glfw3.h> 
#include "implot.h"
#include "gui/render_ofdm_demod.h"
#include "gui/imgui_skeleton.h"
#include "gui/font_awesome_definitions.h"

class App: public ImguiSkeleton
{
private:
    // buffers
    FILE* fp_in;
    std::complex<uint8_t>* buf_rd;
    std::complex<float>* buf_rd_raw;
    const int block_size;
    // objects
    OFDM_Demod* demod;
    OFDM_Demod::State demod_state;
    // runner state
    bool is_running = true;
    bool flag_step = false;
    bool flag_apply_rd_offset = false;
    bool flag_dump_frame = false;
    // runner thread
    std::thread* runner_thread;
public:
    // External controls
    bool is_wait_step = false;
    bool is_always_dump_frame = false;
public:
    App(const int transmission_mode, FILE* const _fp_in, const int _block_size)
    : fp_in(_fp_in), block_size(_block_size)
    {
        buf_rd = new std::complex<uint8_t>[block_size];
        buf_rd_raw = new std::complex<float>[block_size];

        demod_state = OFDM_Demod::State::FINDING_NULL_POWER_DIP;

        // Get our OFDM demodulator and frequency deinterleaver 
        {
            const OFDM_Params ofdm_params = get_DAB_OFDM_params(transmission_mode);
            auto ofdm_prs_ref = new std::complex<float>[ofdm_params.nb_fft];
            get_DAB_PRS_reference(transmission_mode, ofdm_prs_ref, ofdm_params.nb_fft);
            auto ofdm_mapper_ref = new int[ofdm_params.nb_data_carriers];
            get_DAB_mapper_ref(ofdm_mapper_ref, ofdm_params.nb_data_carriers, ofdm_params.nb_fft);

            demod = new OFDM_Demod(ofdm_params, ofdm_prs_ref, ofdm_mapper_ref);
            delete [] ofdm_prs_ref;
            delete [] ofdm_mapper_ref;
        }
        {
            using namespace std::placeholders;
            demod->On_OFDM_Frame().Attach(std::bind(&App::OnOFDMFrame, this, _1, _2, _3));
        }
        {
            auto& cfg = demod->GetConfig();
            cfg.toggle_flags.is_update_data_sym_mag = true;
            cfg.toggle_flags.is_update_tii_sym_mag = true;
        }

        is_running = false;
        runner_thread = NULL;
    }
    ~App() {
        is_running = false;
        fclose(fp_in);
        fp_in = NULL;
        runner_thread->join();
        delete demod;
        delete runner_thread;
        delete [] buf_rd;
        delete [] buf_rd_raw;
    }
    void Start() {
        if (is_running) {
            return;
        }
        is_running = true;
        runner_thread = new std::thread([this]() {
            RunnerThread();
        });
    }
    inline OFDM_Demod::State GetDemodulatorState() const { return demod_state; }
private:
    void OnOFDMFrame(const viterbi_bit_t* phases, const int nb_carriers, const int nb_symbols) {
        if (flag_dump_frame || is_always_dump_frame) {
            const int N = demod->Get_OFDM_Frame_Total_Bits();
            fwrite(phases, sizeof(viterbi_bit_t), N, stdout);
            flag_dump_frame = false;
        }
    }
// Imgui skeleton code
public:
    virtual GLFWwindow* Create_GLFW_Window(void) {
        return glfwCreateWindow(
            1280, 720, 
            "OFDM Demodulator GUI", 
            NULL, NULL);
    }
    virtual void AfterImguiContextInit() {
        ImPlot::CreateContext();
        ImguiSkeleton::AfterImguiContextInit();
        auto& io = ImGui::GetIO();
        io.IniFilename =  "imgui_ofdm_demod.ini";
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
        RenderSourceBuffer(buf_rd_raw, block_size);
        RenderOFDMDemodulator(demod);
        RenderAppControls();
    }
    virtual void AfterShutdown() {
        ImPlot::DestroyContext();
    }
private:
    void RenderAppControls() {
        if (ImGui::Begin("Input controls")) {
            if (ImGui::Button("Offset input stream")) {
                flag_apply_rd_offset = true;
            }
            ImGui::Checkbox("Enable stepping", &is_wait_step);
            if (is_wait_step) {
                if (ImGui::Button("Step")) {
                    flag_step = true;
                }
            }             

            ImGui::Checkbox("Enable continuous frame dump", &is_always_dump_frame);
            if (!is_always_dump_frame) {
                if (ImGui::Button("Dump next block")) {
                    flag_dump_frame = true;
                }
            } 
        }
        ImGui::End();
    }
private:
    void RunnerThread() {
        while (is_running) {
            while (!(flag_step) && (is_wait_step)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(30));
            }
            flag_step = false;

            // NOTE: forcefully induce a single byte desync
            // This is required when the receiver is overloaded and something causes a single byte to be dropped
            // This causes the IQ values to become desynced and produce improper values
            // Inducing another single byte dropout will correct the stream
            if (flag_apply_rd_offset) {
                uint8_t dummy = 0x00;
                if (fp_in == NULL) {
                    return;
                }
                auto nb_read = fread(&dummy, sizeof(uint8_t), 1, fp_in);
                flag_apply_rd_offset = false;
            }

            if (fp_in == NULL) {
                return;
            }
            auto nb_read = fread((void*)buf_rd, sizeof(std::complex<uint8_t>), block_size, fp_in);
            if (nb_read != block_size) {
                fprintf(stderr, "Failed to read in data\n");
                break;
            }

            for (int i = 0; i < block_size; i++) {
                auto& v = buf_rd[i];
                const float I = static_cast<float>(v.real()) - 127.5f;
                const float Q = static_cast<float>(v.imag()) - 127.5f;
                buf_rd_raw[i] = std::complex<float>(I, Q);
            }

            demod->Process(buf_rd_raw, block_size);
            demod_state = demod->GetState();
        }
    }
};


void usage() {
    fprintf(stderr, 
        "ofdm_demod_gui, runs OFDM demodulation on raw IQ values with GUI\n\n"
        "\t[-b block size (default: 8192)]\n"
        "\t[-i input filename (default: None)]\n"
        "\t    If no file is provided then stdin is used\n"
        "\t[-M dab transmission mode (default: 1)]\n"
        "\t[-S toggle step mode (default: false)]\n"
        "\t[-D toggle frame output (default: true)]\n"
        "\t[-h (show usage)]\n"
    );
}

int main(int argc, char** argv) 
{
    int block_size = 8192;
    int transmission_mode = 1;
    bool is_step_mode = false;
    bool is_frame_output = true;
    char* rd_filename = NULL;

    int opt; 
    while ((opt = getopt(argc, argv, "b:i:M:SDh")) != -1) {
        switch (opt) {
        case 'b':
            block_size = (int)(atof(optarg));
            if (block_size <= 0) {
                fprintf(stderr, "Block size must be positive (%d)\n", block_size); 
                return 1;
            }
            break;
        case 'i':
            rd_filename = optarg;
            break;
        case 'M':
            transmission_mode = (int)(atof(optarg));
            break;
        case 'S':
            is_step_mode = true;
            break;
        case 'D':
            is_frame_output = false;
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

    auto app = new App(transmission_mode, fp_in, block_size);
    app->is_wait_step = is_step_mode;
    app->is_always_dump_frame = is_frame_output;
    app->Start();

    RenderImguiSkeleton(app);
    delete app;

    return 0;
}
