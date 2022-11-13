#include <stdio.h>
#include <stdlib.h>
#include <io.h>
#include <fcntl.h>

#include "getopt/getopt.h"

#include <GLFW/glfw3.h>
#include "imgui.h"
#include "gui/imgui_skeleton.h"
#include "gui/font_awesome_definitions.h"
#include "implot.h"

#include <unordered_map>
#include <thread>
#include <string>
#include <memory>
#include <fmt/core.h>

#include "block_frequencies.h"
#include "observable.h"
#include "double_buffer.h"

#include "device/device_selector.h"
#include "ofdm/ofdm_demodulator.h"
#include "ofdm/dab_ofdm_params_ref.h"
#include "ofdm/dab_prs_ref.h"
#include "ofdm/dab_mapper_ref.h"
#include "basic_radio/basic_radio.h"

#include "gui/render_device_selector.h"
#include "gui/render_ofdm_demod.h"
#include "gui/basic_radio/render_simple_view.h"

#include "audio/win32_pcm_player.h"

#include "easylogging++.h"
#include "dab/logging.h"

std::unique_ptr<OFDM_Demod> Init_OFDM_Demodulator(const int transmission_mode) {
	const OFDM_Params ofdm_params = get_DAB_OFDM_params(transmission_mode);
	auto ofdm_prs_ref = new std::complex<float>[ofdm_params.nb_fft];
	get_DAB_PRS_reference(transmission_mode, ofdm_prs_ref, ofdm_params.nb_fft);
	auto ofdm_mapper_ref = new int[ofdm_params.nb_data_carriers];
	get_DAB_mapper_ref(ofdm_mapper_ref, ofdm_params.nb_data_carriers, ofdm_params.nb_fft);

	auto ofdm_demod = std::make_unique<OFDM_Demod>(ofdm_params, ofdm_prs_ref, ofdm_mapper_ref);

	{
		auto& cfg = ofdm_demod->GetConfig();
		cfg.toggle_flags.is_update_data_sym_mag = true;
		cfg.toggle_flags.is_update_tii_sym_mag = true;
	}

	delete [] ofdm_prs_ref;
	delete [] ofdm_mapper_ref;

	return ofdm_demod;
}	

class RadioDependencies: public Basic_Radio_Dependencies 
{
public:
    virtual PCM_Player* Create_PCM_Player(void) {
        return new Win32_PCM_Player();
    }
};

// Class to glue all of the components together
class App 
{
private:
	RadioDependencies dependencies;
	// When switching between block frequencies, interrupt the data flow so we dont have data from 
	// the previous block frequency entering the data models for the new block frequency
	int demodulator_cooldown_max = 10;
	int demodulator_cooldown = 0;
	std::unique_ptr<DeviceSelector> device_selector;
	std::unique_ptr<OFDM_Demod> ofdm_demod;
	// Have a unique radio/view controller for each block frequency
	std::string selected_radio;	// name of radio is the block frequency key
	typedef std::pair<std::unique_ptr<BasicRadio>, std::unique_ptr<SimpleViewController>> radio_instance_t;
	std::unordered_map<std::string, radio_instance_t> basic_radios;
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
		const int transmission_mode = 1;
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

			device->OnData().Attach(std::bind(&App::OnData, this, _1, _2));
			device->OnFrequencyChange().Attach(std::bind(&App::OnFrequencyChange, this, _1, _2));
			device->SetCenterFrequency("9C", block_frequencies.at("9C"));
		});

		ofdm_demod->On_OFDM_Frame().Attach([this](const viterbi_bit_t* buf, const int nb_carriers, const int nb_symbols) {
			auto* inactive_buf = frame_double_buffer->AcquireInactiveBuffer();		
			if (inactive_buf == NULL) {
				return;
			}
			const int N = frame_double_buffer->GetLength();
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
				const int N = frame_double_buffer->GetLength();
				auto instance = GetSelectedRadio();
				auto* basic_radio = instance.first;
				if ((basic_radio != NULL) && (demodulator_cooldown == 0)) {
					basic_radio->Process(active_buf, N);
				}

				frame_double_buffer->ReleaseActiveBuffer();
			}
		});

		device_selector->SelectDevice(0);
	}	
	std::pair<BasicRadio*, SimpleViewController*> GetSelectedRadio() {
		auto res = basic_radios.find(selected_radio);
		if (res == basic_radios.end()) {
			return {NULL, NULL};
		}
		
		auto& instance = res->second;
		return { instance.first.get(), instance.second.get() };
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
				const int N = raw_double_buffer->GetLength();
				ofdm_demod->Process(active_buf, N);
				raw_double_buffer->ReleaseActiveBuffer();
			}
		});
	}
	void OnFrequencyChange(const std::string& label, const uint32_t freq) {
		const int transmission_mode = 1;
		const auto dab_params = get_dab_parameters(transmission_mode);
		demodulator_cooldown = demodulator_cooldown_max;

		auto res = basic_radios.find(label);
		if (res == basic_radios.end()) {
			auto basic_radio = std::make_unique<BasicRadio>(dab_params, &dependencies);
			auto controller = std::make_unique<SimpleViewController>();
			controller->AttachRadio(basic_radio.get());
			basic_radios.insert({
				label, 
				std::make_pair<std::unique_ptr<BasicRadio>, std::unique_ptr<SimpleViewController>>(
					std::move(basic_radio), std::move(controller))
			});
		}
		selected_radio = label;
	}
	void OnData(const std::complex<uint8_t>* data, const int N) {
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
		if (ImGui::Begin("Demodulator Controls")) {
			ImGui::DockSpace(ImGui::GetID("Demodulator Dockspace"));
			RenderDeviceSelector(app.GetDeviceSelector());
			{
				auto& double_buffer = app.GetInputBuffer();
				auto* buf = double_buffer.AcquireInactiveBuffer();
				if (buf != NULL) {
					const int N = double_buffer.GetLength();
					RenderSourceBuffer(buf, N);
				}
			}
			RenderOFDMDemodulator(&app.GetOFDMDemodulator());
		}
		ImGui::End();

		{
			auto instance = app.GetSelectedRadio();
			if (instance.first && instance.second) {
				RenderSimple_Root(instance.first, instance.second);
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

	auto app = App();
	auto renderer = Renderer(app);
	RenderImguiSkeleton(&renderer);

	return 0;
}
