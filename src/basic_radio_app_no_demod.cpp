// Basic radio without the OFDM demodulator
// Only has the DAB digital decoder

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif

#include "audio/portaudio_output.h"
#include "audio/audio_mixer.h"
#include "audio/resampled_pcm_player.h"
#include "audio/portaudio_utility.h"

#include "modules/basic_radio/basic_radio.h"

#include <thread>
#include <memory>
#include <vector>
#include <unordered_map>

#include <GLFW/glfw3.h> 
#include <imgui.h>
#include "gui/imgui_skeleton.h"
#include "gui/font_awesome_definitions.h"
#include "gui/basic_radio/render_simple_view.h"
#include "gui/render_portaudio_controls.h"

#include "utility/getopt/getopt.h"
#include "easylogging++.h"
#include "modules/dab/logging.h"

class App 
{
private:
    FILE* fp_in;

    std::vector<viterbi_bit_t> frame_bits;
    std::unique_ptr<BasicRadio> radio;
    std::unique_ptr<SimpleViewController> gui_controller;
    std::unique_ptr<std::thread> radio_thread;

    PaDeviceList pa_devices;
    PortAudio_Output pa_output;
    std::unordered_map<subchannel_id_t, std::unique_ptr<Resampled_PCM_Player>> dab_plus_audio_players;
public:
    App(const int transmission_mode, FILE* const _fp_in) 
    : fp_in(_fp_in) {
        auto params = get_dab_parameters(transmission_mode);
        frame_bits.resize(params.nb_frame_bits);
        radio = std::make_unique<BasicRadio>(params);
        gui_controller = std::make_unique<SimpleViewController>(*(radio.get()));

		using namespace std::placeholders;
		radio->On_DAB_Plus_Channel().Attach(std::bind(&App::Attach_DAB_Plus_Audio_Player, this, _1, _2));

        radio_thread = std::make_unique<std::thread>([this]() {
            RunnerThread();
        });

        #ifdef _WIN32
        const auto target_host_api_index = Pa_HostApiTypeIdToHostApiIndex(PORTAUDIO_TARGET_HOST_API_ID);
        const auto target_device_index = Pa_GetHostApiInfo(target_host_api_index)->defaultOutputDevice;
        pa_output.Open(target_device_index);
        #else
        pa_output.Open(Pa_GetDefaultOutputDevice());
        #endif
    }
    ~App() {
        fclose(fp_in);
        fp_in = NULL;
        radio_thread->join();
    }
    auto& GetRadio() { return *(radio.get()); }
    auto& GetViewController() { return *(gui_controller.get()); }
    auto& GetPaAudioOutput() { return pa_output; }
    auto& GetPaDevices() { return pa_devices; }
private:
    void RunnerThread() {
        while (true) {
            if (fp_in == NULL) return;

            const size_t N = frame_bits.size();
            const size_t nb_read = fread(frame_bits.data(), sizeof(viterbi_bit_t), N, fp_in);
            if (nb_read != N) {
                fprintf(stderr, "Failed to read soft-decision bits (%zu/%zu)\n", nb_read, N);
                break;
            }
            radio->Process(frame_bits);
        }
    }
	void Attach_DAB_Plus_Audio_Player(subchannel_id_t subchannel_id, Basic_DAB_Plus_Channel& channel) {
		auto& controls = channel.GetControls();

        auto& mixer = pa_output.GetMixer();
        auto buf = mixer.CreateManagedBuffer(2);

        auto player = std::make_unique<Resampled_PCM_Player>(buf, pa_output.GetSampleRate());
		auto res = dab_plus_audio_players.emplace(subchannel_id, std::move(player)).first;
		auto* pcm_player = res->second.get(); 
		channel.OnAudioData().Attach([this, &controls, pcm_player](BasicAudioParams params, tcb::span<const uint8_t> buf) {
			if (!controls.GetIsPlayAudio()) {
				return;
			}

            pcm_player->SetInputSampleRate(params.frequency);

            tcb::span<const Frame<int16_t>> rd_buf = {
                reinterpret_cast<const Frame<int16_t>*>(buf.data()),
                (size_t)(buf.size() / sizeof(Frame<int16_t>))
            };

			pcm_player->ConsumeBuffer(rd_buf);
		});
	}
};

class Renderer: public ImguiSkeleton 
{
private:
    App& app;
public:
    Renderer(App& _app)
    : app(_app)
    {}
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
        if (ImGui::Begin("Simple View")) {
            ImGuiID dockspace_id = ImGui::GetID("Simple View Dockspace");
            ImGui::DockSpace(dockspace_id);
            if (ImGui::Begin("Audio Controls")) {
                RenderPortAudioControls(app.GetPaDevices(), app.GetPaAudioOutput());
            }
            ImGui::End();
            RenderSimple_Root(app.GetRadio(), app.GetViewController());
        }
        ImGui::End();

    }
};

void usage() {
    fprintf(stderr, 
        "basic_radio_app_no_demod, decodes logical OFDM frame as a DAB transmission into a basic radio\n\n"
        "\t[-i input filename (default: None)]\n"
        "\t    If no file is provided then stdin is used\n"
        "\t[-M dab transmission mode (default: 1)]\n"
        "\t[-v Enable logging (default: false)]\n"
        "\t[-h (show usage)]\n"
    );
}

INITIALIZE_EASYLOGGINGPP
int main(int argc, char** argv) {
    char* rd_filename = NULL;
    bool is_logging = false;
    int transmission_mode = 1;

    int opt; 
    while ((opt = getopt(argc, argv, "i:M:vh")) != -1) {
        switch (opt) {
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
    _setmode(_fileno(stdout), _O_BINARY);
#endif

    auto dab_loggers = RegisterLogging();
    auto basic_radio_logger = el::Loggers::getLogger("basic-radio");

    el::Configurations defaultConf;
    const char* logging_level = is_logging ? "true" : "false";
    defaultConf.setToDefault();
    defaultConf.setGlobally(el::ConfigurationType::Enabled, logging_level);
    defaultConf.setGlobally(el::ConfigurationType::Format, "[%level] [%thread] [%logger] %msg");
    el::Loggers::reconfigureAllLoggers(defaultConf);
    el::Helpers::setThreadName("main-thread");

    auto port_audio_handler = ScopedPaHandler();
    auto app = App(transmission_mode, fp_in);
    auto renderer = Renderer(app);
    const int rv = RenderImguiSkeleton(&renderer);
    return rv;
}

