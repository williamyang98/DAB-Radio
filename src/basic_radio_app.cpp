// Basic radio app that includes the OFDM demodulator and the DAB digital decoder

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

#include "gui/render_ofdm_demod.h"
#include "gui/render_basic_radio.h"
#include "gui/imgui_skeleton.h"
#include "gui/font_awesome_definitions.h"

#include <GLFW/glfw3.h> 
#include "imgui.h"
#include "implot.h"

#include <mutex>
#include <condition_variable>
#include <thread>

// Class that connects our analog OFDM demodulator and digital DAB decoder
// Also provides the skeleton of the Imgui application
class App: public ImguiSkeleton 
{
private:
    // Number of bytes per OFDM frame in transmission mode I
    // NOTE: we are hard coding this because all other transmission modes have been deprecated
    FILE* const fp_in;
    const int block_size;
    std::complex<uint8_t>* rd_in_raw;
    std::complex<float>* rd_in_float;
    // We need this if the rtl_sdr.exe app drops a byte and causes the I and Q channels to swap
    bool flag_rd_byte_offset = false;

    // Double buffer our demodulator frame data to radio thread
    viterbi_bit_t* inactive_frame_buffer;
    viterbi_bit_t* active_frame_buffer;
    int nb_frame_bits;
    
    bool is_start_buffer;
    std::mutex mutex_start_buffer;
    std::condition_variable cv_start_buffer;

    bool is_end_buffer;
    std::mutex mutex_end_buffer;
    std::condition_variable cv_end_buffer;

    // Blocks that make our radio
    bool is_running;
    OFDM_Demod* ofdm_demod;
    BasicRadio* radio;

    // Separate threads for the radio, and raw IQ to OFDM frame demodulator
    std::thread* ofdm_demod_thread;
    std::thread* basic_radio_thread;
public:
    App(FILE* const _fp_in, const int _block_size)
    : fp_in(_fp_in), block_size(_block_size)
    {
        const int transmission_mode = 1;
        radio = new BasicRadio(get_dab_parameters(transmission_mode));
        Init_OFDM_Demodulator(transmission_mode);

        // Buffer to read raw IQ 8bit values and convert to floating point
        rd_in_raw = new std::complex<uint8_t>[block_size];
        rd_in_float = new std::complex<float>[block_size];

        // Create our runner threads
        is_running = false;
        ofdm_demod_thread = NULL;
        basic_radio_thread = NULL;
        Start();
    }
    ~App() {
        if (ofdm_demod_thread != NULL) {
            is_running = false;
            fclose(fp_in);
            ofdm_demod_thread->join();
            delete ofdm_demod_thread;
        }
        if (basic_radio_thread != NULL) {
            is_running = false;
            SignalStartBuffer();
            basic_radio_thread->join();
            delete basic_radio_thread;
        }
        delete radio;

        // NOTE: we do this so that the double buffer call 
        //       will exit when the buffering thread is closed
        SignalEndBuffer();
        delete ofdm_demod;
        delete [] rd_in_raw;
        delete [] rd_in_float;
        delete [] active_frame_buffer;
        delete [] inactive_frame_buffer;
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

        nb_frame_bits = ofdm_demod->Get_OFDM_Frame_Total_Bits();
        active_frame_buffer = new viterbi_bit_t[nb_frame_bits];
        inactive_frame_buffer = new viterbi_bit_t[nb_frame_bits];
        is_start_buffer = false;
        is_end_buffer = false;
        {
            using namespace std::placeholders;
            ofdm_demod->On_OFDM_Frame().Attach(std::bind(&App::DoubleBufferFrameData, this, _1, _2, _3));
        } 
        {
            auto& cfg = ofdm_demod->GetConfig();
            cfg.toggle_flags.is_update_data_sym_mag = true;
            cfg.toggle_flags.is_update_tii_sym_mag = true;
        }

        delete [] ofdm_prs_ref;
        delete [] ofdm_mapper_ref;
    }
    void Start() {
        is_running = true;
        SignalEndBuffer();
        basic_radio_thread = new std::thread([this]() {
            RunnerThread_Radio();
        });
        ofdm_demod_thread = new std::thread([this]() {
            RunnerThread_OFDM_Demod();
        });
    }
private:
    void SignalStartBuffer() {
        auto lock = std::scoped_lock(mutex_start_buffer);
        is_start_buffer = true;
        cv_start_buffer.notify_one();
    }
    void WaitStartBuffer() {
        auto lock = std::unique_lock(mutex_start_buffer);
        cv_start_buffer.wait(lock, [this]() { return is_start_buffer; });
        is_start_buffer = false;
    }
    void SignalEndBuffer() {
        auto lock = std::scoped_lock(mutex_end_buffer);
        is_end_buffer = true;
        cv_end_buffer.notify_one();
    }
    void WaitEndBuffer() {
        auto lock = std::unique_lock(mutex_end_buffer);
        cv_end_buffer.wait(lock, [this]() { return is_end_buffer; });
        is_end_buffer = false;
    }
    void DoubleBufferFrameData(const viterbi_bit_t* buf, const int nb_carriers, const int nb_symbols) {
        for (int i = 0; i < nb_frame_bits; i++) {
            inactive_frame_buffer[i] = buf[i];
        }
        // NOTE: this stops a deadlock on application exit from occuring when the double buffer thread terminates
        WaitEndBuffer();
        if (!is_running) {
            return;
        }
        auto* tmp = inactive_frame_buffer;
        inactive_frame_buffer = active_frame_buffer;
        active_frame_buffer = tmp; 
        SignalStartBuffer();
    }
// All our imgui skeleton code
public:
    virtual GLFWwindow* Create_GLFW_Window(void) {
        return glfwCreateWindow(
            1280, 720, 
            "Complete DAB Radio", 
            NULL, NULL);
    }
    virtual void AfterImguiContextInit() {
        ImPlot::CreateContext();
        ImguiSkeleton::AfterImguiContextInit();

        auto& io = ImGui::GetIO();
        io.IniFilename =  "imgui_basic_radio.ini";
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
        if (ImGui::Begin("Demodulator")) {
            ImGuiID dockspace_id = ImGui::GetID("Demodulator Dockspace");
            ImGui::DockSpace(dockspace_id);
            RenderSourceBuffer(rd_in_float, block_size);
            RenderOFDMDemodulator(ofdm_demod);
            RenderAppControls();
        }
        RenderBasicRadio(radio);
        ImGui::End();
    }
    virtual void AfterShutdown() {
        ImPlot::DestroyContext();
    }
// Controls for reading raw IQ values
private:
    void RenderAppControls() {
        if (ImGui::Begin("Input controls")) {
            ImGui::Text(
                "If the impulse response doesn't have a very sharp peak then we have a byte desync\n"
                "This arises when the rtl_sdr.exe app drops a byte, causing the I and Q channels to swap\n"
                "Although we still get a correlation peak from the phase reference symbol, the phase information is incorrect\n",
                "This causes havoc on the fine frequency correction and time synchronisation\n"
                "Press the offset input stream button to correct this desynchronisation");
            if (ImGui::Button("Offset input stream")) {
                flag_rd_byte_offset = true;
            }
        }
        ImGui::End();
    }
// Runner threads for the OFDM demodulator and DAB radio decoder
private:
    void RunnerThread_OFDM_Demod() {
        while (is_running) {
            // Resynchronise the IQ values if the rtl_sdr.exe app dropped a byte
            if (flag_rd_byte_offset) {
                uint8_t dummy = 0x00;
                auto nb_read = fread(&dummy, sizeof(uint8_t), 1, fp_in);
                flag_rd_byte_offset = false;
            }

            // Read raw 8bit IQ values and convert them to floating point
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
    void RunnerThread_Radio() {
        while (is_running) {
            WaitStartBuffer();
            if (!is_running) {
                break;
            }
            radio->Process(active_frame_buffer, nb_frame_bits);
            SignalEndBuffer();
        }
        SignalEndBuffer();
    }
};

void usage() {
    fprintf(stderr, 
        "process_frames, decoded DAB frame data\n\n"
        "\t[-i input filename (default: None)]\n"
        "\t    If no file is provided then stdin is used\n"
        "\t[-v Enable logging (default: false)]\n"
        "\t[-b block size (default: 8192)]\n"
        "\t[-h (show usage)]\n"
    );
}

INITIALIZE_EASYLOGGINGPP
int main(int argc, char** argv) {
    char* rd_filename = NULL;
    int block_size = 8192;
    bool is_logging = false;

    int opt; 
    while ((opt = getopt(argc, argv, "i:b:vh")) != -1) {
        switch (opt) {
        case 'i':
            rd_filename = optarg;
            break;
        case 'b':
            block_size = (int)(atof(optarg));
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
    defaultConf.setGlobally(el::ConfigurationType::Enabled, logging_level);
    defaultConf.setGlobally(el::ConfigurationType::Format, "[%level] [%thread] [%logger] %msg");
    el::Loggers::reconfigureAllLoggers(defaultConf);
    el::Helpers::setThreadName("main-thread");

    auto app = new App(fp_in, block_size);
    const int rv = RenderImguiSkeleton(app);
    delete app;
    return rv;
}

