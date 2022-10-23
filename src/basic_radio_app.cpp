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
#include "ofdm/ofdm_symbol_mapper.h"
#include "ofdm/dab_ofdm_params_ref.h"
#include "ofdm/dab_prs_ref.h"
#include "ofdm/dab_mapper_ref.h"
#include "basic_radio.h"

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
    uint8_t* frame_double_buffer;
    int nb_frame_bytes;
    bool is_double_buffer_ready = false;
    std::mutex mutex_double_buffer;
    std::condition_variable cv_double_buffer;

    // Blocks that make our radio
    bool is_running;
    OFDM_Demodulator* ofdm_demod;
    OFDM_Symbol_Mapper* ofdm_sym_mapper;
    BasicRadio* radio;

    // Separate threads for the radio, and raw IQ to OFDM frame demodulator
    std::thread* ofdm_demod_thread;
    std::thread* basic_radio_thread;
public:
    App(FILE* const _fp_in, const int _block_size)
    : fp_in(_fp_in), block_size(_block_size)
    {
        radio = new BasicRadio();
        Init_OFDM_Demodulator();

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
            ofdm_demod_thread->join();
            delete ofdm_demod_thread;
        }
        if (basic_radio_thread != NULL) {
            is_running = false;
            {
                auto lock = std::scoped_lock(mutex_double_buffer);
                is_double_buffer_ready = true;
                cv_double_buffer.notify_one();
            }
            basic_radio_thread->join();
            delete basic_radio_thread;
        }
        delete radio;
        delete ofdm_demod;
        delete ofdm_sym_mapper;
        delete [] rd_in_raw;
        delete [] rd_in_float;
        delete [] frame_double_buffer;
    }
private:
    // Get our OFDM demodulator and frequency deinterleaver 
    void Init_OFDM_Demodulator() {
        const int transmission_mode = 1;
        const OFDM_Params ofdm_params = get_DAB_OFDM_params(transmission_mode);
        auto ofdm_prs_ref = new std::complex<float>[ofdm_params.nb_fft];
        get_DAB_PRS_reference(transmission_mode, ofdm_prs_ref, ofdm_params.nb_fft);
        auto ofdm_mapper_ref = new int[ofdm_params.nb_data_carriers];
        get_DAB_mapper_ref(ofdm_mapper_ref, ofdm_params.nb_data_carriers, ofdm_params.nb_fft);
        ofdm_demod = new OFDM_Demodulator(ofdm_params, ofdm_prs_ref);
        // due to differential encoding, the PRS doesn't count 
        ofdm_sym_mapper = new OFDM_Symbol_Mapper(
            ofdm_mapper_ref, ofdm_params.nb_data_carriers, 
            ofdm_params.nb_frame_symbols-1);

        nb_frame_bytes = ofdm_sym_mapper->GetOutputBufferSize();
        frame_double_buffer = new uint8_t[nb_frame_bytes];
        {
            using namespace std::placeholders;
            ofdm_demod->On_OFDM_Frame().Attach(std::bind(&App::DoubleBufferFrameData, this, _1, _2, _3));
        } 

        delete [] ofdm_prs_ref;
        delete [] ofdm_mapper_ref;
    }
    void DoubleBufferFrameData(const uint8_t* buf, const int nb_carriers, const int nb_symbols) {
        assert(ofdm_sym_mapper->GetTotalCarriers() == nb_carriers);
        assert(ofdm_sym_mapper->GetTotalSymbols() == nb_symbols);
        ofdm_sym_mapper->ProcessRawFrame(buf);

        const auto frame_buf = ofdm_sym_mapper->GetOutputBuffer();

        auto lock = std::scoped_lock(mutex_double_buffer);
        for (int i = 0; i < nb_frame_bytes; i++) {
            frame_double_buffer[i] = frame_buf[i];
        }
        is_double_buffer_ready = true;
        cv_double_buffer.notify_one();
    }
    void Start() {
        is_running = true;
        basic_radio_thread = new std::thread([this]() {
            RunnerThread_Radio();
        });
        ofdm_demod_thread = new std::thread([this]() {
            RunnerThread_OFDM_Demod();
        });
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
        RenderBasicRadio(radio);
        if (ImGui::Begin("Demodulator")) {
            ImGuiID dockspace_id = ImGui::GetID("Demodulator Dockspace");
            ImGui::DockSpace(dockspace_id);
            RenderSourceBuffer(rd_in_float, block_size);
            RenderOFDMDemodulator(ofdm_demod, ofdm_sym_mapper);
            RenderAppControls();
        }
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

            ofdm_demod->ProcessBlock(rd_in_float, block_size);
        }
    }    
    void RunnerThread_Radio() {
        while (is_running) {
            auto lock = std::unique_lock(mutex_double_buffer);
            cv_double_buffer.wait(lock, [this]() { 
                return is_double_buffer_ready; 
            });
            is_double_buffer_ready = false;

            if (!is_running) {
                break;
            }

            radio->ProcessFrame(frame_double_buffer, nb_frame_bytes);
        }
    }
};

void usage() {
    fprintf(stderr, 
        "process_frames, decoded DAB frame data\n\n"
        "\t[-i input filename (default: None)]\n"
        "\t    If no file is provided then stdin is used\n"
        "\t[-b block size (default: 8192)]\n"
        "\t[-h (show usage)]\n"
    );
}

INITIALIZE_EASYLOGGINGPP
int main(int argc, char** argv) {
    char* rd_filename = NULL;
    int block_size = 8192;

    int opt; 
    while ((opt = getopt(argc, argv, "i:b:h")) != -1) {
        switch (opt) {
        case 'i':
            rd_filename = optarg;
            break;
        case 'b':
            block_size = (int)(atof(optarg));
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
    defaultConf.setToDefault();
    defaultConf.set(el::Level::Error,   el::ConfigurationType::Enabled, "true");
    defaultConf.set(el::Level::Warning, el::ConfigurationType::Enabled, "true");
    defaultConf.set(el::Level::Info,    el::ConfigurationType::Enabled, "true");
    defaultConf.set(el::Level::Debug,   el::ConfigurationType::Enabled, "true");
    el::Loggers::reconfigureAllLoggers(defaultConf);

    auto app = new App(fp_in, block_size);
    const int rv = RenderImguiSkeleton(app);
    delete app;
    return rv;
}

