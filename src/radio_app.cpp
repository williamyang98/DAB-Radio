#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <implot.h>
#include "gui/imgui_skeleton.h"
#include "gui/font_awesome_definitions.h"

#include <unordered_map>
#include <thread>
#include <string>
#include <memory>
#include <fmt/core.h>

#include "modules/device/device_selector.h"
#include "modules/ofdm/ofdm_demodulator.h"
#include "modules/ofdm/dab_ofdm_params_ref.h"
#include "modules/ofdm/dab_prs_ref.h"
#include "modules/ofdm/dab_mapper_ref.h"
#include "modules/basic_radio/basic_radio.h"
#include "gui/render_device_selector.h"
#include "gui/render_ofdm_demod.h"
#include "gui/basic_radio/render_simple_view.h"
#include "gui/render_profiler.h"
#include "audio/win32_pcm_player.h"

#include "block_frequencies.h"
#include "utility/double_buffer.h"

#include "utility/getopt/getopt.h"
#include "easylogging++.h"
#include "modules/dab/logging.h"

std::unique_ptr<OFDM_Demod> Init_OFDM_Demodulator(const int transmission_mode) {
	const OFDM_Params ofdm_params = get_DAB_OFDM_params(transmission_mode);
	auto ofdm_prs_ref = std::vector<std::complex<float>>(ofdm_params.nb_fft);
	get_DAB_PRS_reference(transmission_mode, ofdm_prs_ref);
	auto ofdm_mapper_ref = std::vector<int>(ofdm_params.nb_data_carriers);
	get_DAB_mapper_ref(ofdm_mapper_ref, ofdm_params.nb_fft);
	auto ofdm_demod = std::make_unique<OFDM_Demod>(ofdm_params, ofdm_prs_ref, ofdm_mapper_ref, 1);
	return std::move(ofdm_demod);
}

class RadioInstance 
{
private:
	std::unique_ptr<BasicRadio> radio;
	std::unique_ptr<SimpleViewController> view_controller;	
	std::unordered_map<subchannel_id_t, std::unique_ptr<PCM_Player>> dab_plus_audio_players;
public:
	RadioInstance(std::unique_ptr<BasicRadio> _radio, std::unique_ptr<SimpleViewController> _view_controller)
	: dab_plus_audio_players() 
	{
		radio = std::move(_radio);
		view_controller = std::move(_view_controller);

		using namespace std::placeholders;
		radio->On_DAB_Plus_Channel().Attach(std::bind(&RadioInstance::Attach_DAB_Plus_Audio_Player, this, _1, _2));
	}
	// We bind callbacks to this instance so we can't move or copy it
	RadioInstance(const RadioInstance &) = delete;
	RadioInstance(RadioInstance &&) = delete;
	RadioInstance &operator=(const RadioInstance &) = delete;
	RadioInstance &operator=(RadioInstance &&) = delete;
	auto& GetRadio() { return *(radio.get()); }
	auto& GetViewController() { return *(view_controller.get()); }
private:
	void Attach_DAB_Plus_Audio_Player(subchannel_id_t subchannel_id, Basic_DAB_Plus_Channel& channel) {
		auto& controls = channel.GetControls();
		auto res = dab_plus_audio_players.emplace(
			subchannel_id, 
			std::move(std::make_unique<Win32_PCM_Player>())).first;
		auto* pcm_player = res->second.get(); 
		channel.OnAudioData().Attach([this, &controls, pcm_player](BasicAudioParams params, tcb::span<const uint8_t> data) {
			if (!controls.GetIsPlayAudio()) {
				return;
			}

			auto pcm_params = pcm_player->GetParameters();
			pcm_params.sample_rate = params.frequency;
			pcm_params.total_channels = 2;
			pcm_params.bytes_per_sample = 2;
			pcm_player->SetParameters(pcm_params);
			pcm_player->ConsumeBuffer(data);
		});
	}
};

// Class to glue all of the components together
class App 
{
private:
	const int transmission_mode = 1;
	// When switching between block frequencies, interrupt the data flow so we dont have data from 
	// the previous block frequency entering the data models for the new block frequency
	int demodulator_cooldown_max = 10;
	int demodulator_cooldown = 0;
	std::unique_ptr<DeviceSelector> device_selector;
	std::unique_ptr<OFDM_Demod> ofdm_demod;
	// Have a unique radio/view controller for each block frequency
	std::string selected_radio;	// name of radio is the block frequency key
	std::unordered_map<std::string, std::unique_ptr<RadioInstance>> basic_radios;
	// Double buffer data flow
	// rtlsdr_device -> double_buffer -> ofdm_demodulator -> double_buffer -> basic_radio
	std::unique_ptr<DoubleBuffer<std::complex<float>>> raw_double_buffer;
	std::unique_ptr<DoubleBuffer<viterbi_bit_t>> frame_double_buffer;
	// rtlsdr_device, ofdm_demodulator, basic_radio all operate on separate threads
	std::unique_ptr<std::thread> ofdm_demod_thread;
	std::unique_ptr<std::thread> basic_radio_thread;
public:
	App() {
		device_selector = std::make_unique<DeviceSelector>();
		const auto dab_params = get_dab_parameters(transmission_mode);
		ofdm_demod = Init_OFDM_Demodulator(transmission_mode);

		frame_double_buffer = std::make_unique<DoubleBuffer<viterbi_bit_t>>(dab_params.nb_frame_bits);
		raw_double_buffer = std::make_unique<DoubleBuffer<std::complex<float>>>(0);

		device_selector->OnDeviceChange().Attach([this](Device* device) {
			if (device == NULL) {
				return;
			}
			using namespace std::placeholders;

			const int N = device->GetTotalSamples();
			OnTotalSamplesChanged(N);

			device->OnData().Attach(std::bind(&App::OnData, this, _1));
			device->OnFrequencyChange().Attach(std::bind(&App::OnFrequencyChange, this, _1, _2));
			device->SetCenterFrequency("9C", block_frequencies.at("9C"));
		});

		ofdm_demod->On_OFDM_Frame().Attach([this](tcb::span<const viterbi_bit_t> buf) {
			auto* inactive_buf = frame_double_buffer->AcquireInactiveBuffer();		
			if (inactive_buf == NULL) {
				return;
			}
			const size_t N = frame_double_buffer->GetLength();
			for (int i = 0; i < N; i++) {
				inactive_buf[i] = buf[i];
			}
			frame_double_buffer->ReleaseInactiveBuffer();
		});

		basic_radio_thread = std::make_unique<std::thread>([this]() {
			while (true) {
				auto* active_buf = frame_double_buffer->AcquireActiveBuffer();
				if (active_buf == NULL) {
					return;
				}
				const size_t N = frame_double_buffer->GetLength();
				auto instance = GetSelectedRadio();
				if ((instance != NULL) && (demodulator_cooldown == 0)) {
					instance->GetRadio().Process({active_buf, N});
				}

				frame_double_buffer->ReleaseActiveBuffer();
			}
		});
	}	
	~App() {
		raw_double_buffer->Close();
		frame_double_buffer->Close();
		ofdm_demod_thread->join();
		basic_radio_thread->join();
		// NOTE: We delete this to close the rtlsdr async callback
		// Otherwise the callback might fire and call OnData
		// Since this occurs after the App destructor entities like double_buffer may be deleted 
		// which causes an invalid memory access (Caught with address sanitiser)
		device_selector = NULL;
	}
	RadioInstance* GetSelectedRadio() {
		auto res = basic_radios.find(selected_radio);
		if (res == basic_radios.end()) {
			return NULL;
		}
		
		return res->second.get();
	}
	auto& GetOFDMDemodulator(void) { return *(ofdm_demod.get()); }
	auto& GetInputBuffer(void) { return *(raw_double_buffer.get()); }
	auto& GetDeviceSelector(void) { return *(device_selector.get()); }
private:
	void OnTotalSamplesChanged(const int N) {
		if (raw_double_buffer->GetLength() == N) {
			return;
		}

		raw_double_buffer->Close();
		if (ofdm_demod_thread != NULL) {
			ofdm_demod_thread->join();
		}

		raw_double_buffer = std::make_unique<DoubleBuffer<std::complex<float>>>(N);
		ofdm_demod_thread = std::make_unique<std::thread>([this]() {
			while (true) {
				auto* active_buf = raw_double_buffer->AcquireActiveBuffer();
				if (active_buf == NULL) {
					return;
				}
				const size_t N = raw_double_buffer->GetLength();
				ofdm_demod->Process({active_buf, N});
				raw_double_buffer->ReleaseActiveBuffer();
			}
		});
	}
	void OnFrequencyChange(const std::string& label, const uint32_t freq) {
		demodulator_cooldown = demodulator_cooldown_max;

		auto res = basic_radios.find(label);
		if (res == basic_radios.end()) {
			CreateRadio(label);
		}
		selected_radio = label;
	}
	void OnData(tcb::span<const std::complex<uint8_t>> data) {
		const int N = (int)data.size();
		if (demodulator_cooldown > 0) {
			demodulator_cooldown--;
			ofdm_demod->Reset();
			return;
		}

		auto* inactive_buf = raw_double_buffer->AcquireInactiveBuffer();
		if (inactive_buf == NULL) {
			return;
		}
		for (int i = 0; i < N; i++) {
			const float I = static_cast<float>(data[i].real()) - 127.0f;
			const float Q = static_cast<float>(data[i].imag()) - 127.0f;
			inactive_buf[i] = std::complex<float>(I, Q);
		}
		raw_double_buffer->ReleaseInactiveBuffer();
	}
private:
	void CreateRadio(const std::string& key) {
		const auto dab_params = get_dab_parameters(transmission_mode);
		auto radio = std::make_unique<BasicRadio>(dab_params);
		auto view_controller = std::make_unique<SimpleViewController>(*(radio.get()));
		auto instance = std::make_unique<RadioInstance>(
			std::move(radio), 
			std::move(view_controller));
		basic_radios.insert({key, std::move(instance)});
	}
};

class Renderer: public ImguiSkeleton 
{
private:
	App& app;
public:
    Renderer(App& _app): app(_app) {}
public:
    virtual GLFWwindow* Create_GLFW_Window(void) {
        return glfwCreateWindow(
            1280, 720, 
            "Radio App", 
            NULL, NULL);
    }
    virtual void AfterImguiContextInit() {
		ImPlot::CreateContext();
        ImguiSkeleton::AfterImguiContextInit();

        auto& io = ImGui::GetIO();
        io.IniFilename =  "imgui_radio_app.ini";
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
    virtual void AfterShutdown() {
        ImPlot::DestroyContext();
    }
    virtual void Render() {
		RenderProfiler();

		if (ImGui::Begin("Demodulator Controls")) {
			ImGui::DockSpace(ImGui::GetID("Demodulator Dockspace"));
			RenderDeviceSelector(app.GetDeviceSelector(), block_frequencies);
			{
				auto& double_buffer = app.GetInputBuffer();
				auto* buf = double_buffer.AcquireInactiveBuffer();
				if (buf != NULL) {
					const size_t N = double_buffer.GetLength();
					RenderSourceBuffer({buf, N});
				}
			}
			RenderOFDMDemodulator(app.GetOFDMDemodulator());
		}
		ImGui::End();

		{
			auto* instance = app.GetSelectedRadio();
			if (instance != NULL) {
				RenderSimple_Root(instance->GetRadio(), instance->GetViewController());
			}
		}
    }
};

void usage() {
    fprintf(stderr, 
        "radio_app, Complete radio app with device selector, demodulator, dab decoding\n\n"
        "\t[-v Enable logging (default: false)]\n"
        "\t[-h (show usage)]\n"
    );
}

INITIALIZE_EASYLOGGINGPP
int main(int argc, char **argv)
{
	bool is_logging = false;
	int opt; 
    while ((opt = getopt(argc, argv, "vh")) != -1) {
        switch (opt) {
        case 'v':
            is_logging = true;
            break;
        case 'h':
        default:
            usage();
            return 0;
        }
    }

#ifdef _WIN32
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

	auto app = App();
	auto renderer = Renderer(app);
	// Automatically select the first rtlsdr dongle we find
	auto init_command_thread = std::thread([&app]() {
		auto& device_selector = app.GetDeviceSelector();
		device_selector.SearchDevices();
		if (device_selector.GetDeviceList().size() > 0) {
			device_selector.SelectDevice(0);
		}
	});
	RenderImguiSkeleton(&renderer);
	init_command_thread.join();

	return 0;
}
