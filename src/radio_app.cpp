/*
 * rtl-sdr, turns your Realtek RTL2832 based DVB dongle into a SDR receiver
 * Copyright (C) 2012 by Steve Markgraf <steve@steve-m.de>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <errno.h>
#include <signal.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef _WIN32
#include <unistd.h>
#else
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#include "getopt/getopt.h"
#endif

extern "C" {
#include "rtl-sdr.h"
#include "convenience.h"
}

#define DEFAULT_SAMPLE_RATE		2048000
#define DEFAULT_BUF_LENGTH		(16 * 16384)
#define MINIMAL_BUF_LENGTH		512
#define MAXIMAL_BUF_LENGTH		(256 * 16384)

#include <GLFW/glfw3.h>
#include "imgui.h"
#include "gui/imgui_skeleton.h"
#include "gui/font_awesome_definitions.h"
#include "implot.h"

#include <thread>
#include <vector>
#include <string>
#include <list>

#include <fmt/core.h>
#include <memory>
#include <complex>

#include "block_frequencies.h"
#include "observable.h"
#include "double_buffer.h"

#include "ofdm/ofdm_demodulator.h"
#include "ofdm/dab_ofdm_params_ref.h"
#include "ofdm/dab_prs_ref.h"
#include "ofdm/dab_mapper_ref.h"
#include "basic_radio/basic_radio.h"
#include "gui/render_ofdm_demod.h"
#include "gui/basic_radio/render_simple_view.h"
#include "audio/win32_pcm_player.h"

#include "easylogging++.h"
#include "dab/logging.h"

struct DeviceDescriptor {
	int index;
	std::string vendor;
	std::string product;
	std::string serial;
};

class Device 
{
private:
	DeviceDescriptor descriptor;
	rtlsdr_dev_t* device;
	const int block_size;

	std::vector<float> gain_list;
	bool is_gain_manual;
	float selected_gain;
	uint32_t selected_frequency;
	std::string selected_frequency_label;
	std::unique_ptr<std::thread> runner_thread;
	std::list<std::string> error_list;

	Observable<const std::complex<uint8_t>*, const int> obs_on_data;
	Observable<const std::string&, const uint32_t> obs_on_center_frequency;
public:
	Device(rtlsdr_dev_t* _device, const DeviceDescriptor& _descriptor, const int block_multiple=1)
	: device(_device), descriptor(_descriptor),
	  block_size(16384*block_multiple)
	{
		is_gain_manual = true;
		selected_gain = 0.0f;

		SearchGains();
		// SetAutoGain();
		SetNearestGain(19.0f);
		SetSamplingFrequency(2048000);
		rtlsdr_set_bias_tee(device, 0);
		rtlsdr_reset_buffer(device);

		runner_thread = std::make_unique<std::thread>([this]() {
			rtlsdr_read_async(
				device, 
				&Device::rtlsdr_callback, 
				reinterpret_cast<void*>(this), 0, block_size);
		});
	}
	~Device() {
		rtlsdr_cancel_async(device);
		runner_thread->join();
		rtlsdr_close(device);
	}
public:
	const auto& GetDescriptor() { return descriptor; }
	const int GetBlockSize(void) { return block_size; }
	const auto& GetGainList(void) { return gain_list; }
	bool GetIsGainManual(void) { return is_gain_manual; }
	float GetSelectedGain(void) { return selected_gain; }
	uint32_t GetSelectedFrequency(void) { return selected_frequency; }
	const auto& GetSelectedFrequencyLabel(void) { return selected_frequency_label; }
	auto& GetErrorList(void) { return error_list; }
public:
	auto& OnData(void) { return obs_on_data; }
	auto& OnFrequencyChange(void) { return obs_on_center_frequency; }
public:
	void SetAutoGain(void) {
		int r = rtlsdr_set_tuner_gain_mode(device, 0);
		if (r < 0) {
			error_list.push_back("Couldn't set tuner gain mode to automatic");
			return;
		}
		is_gain_manual = false;
		selected_gain = 0.0f;
	}
	void SetNearestGain(const float target_gain) {
		float min_err = 10000.0f;
		float nearest_gain = 0.0f;
		for (auto& gain: gain_list) {
			const float err = std::abs(gain-target_gain);
			if (err < min_err) {
				min_err = err;
				nearest_gain = gain;
			}
		}
		SetGain(nearest_gain);
	}
	void SetGain(const float gain) {
		const int qgain = static_cast<int>(gain*10.0f);
		int r;
		r = rtlsdr_set_tuner_gain_mode(device, 1);
		if (r < 0) {
			error_list.push_back("Couldn't set tuner gain mode to manual");
			return;
		}
		r = rtlsdr_set_tuner_gain(device, qgain);
		if (r < 0) {
			error_list.push_back(fmt::format("Couldn't set manual gain to {:.1f}dB", gain));
			return;
		}
		is_gain_manual = true;
		selected_gain = gain;
	}
	void SetSamplingFrequency(const uint32_t freq) {
		const int r = rtlsdr_set_sample_rate(device, freq);
		if (r < 0) {
			error_list.push_back(fmt::format("Couldn't set sampling frequency to {}", freq));
			return;
		}
	}
	void SetCenterFrequency(const uint32_t freq) {
		SetCenterFrequency("Manual", freq);
	}
	void SetCenterFrequency(const std::string& label, const uint32_t freq) {
		obs_on_center_frequency.Notify(label, freq);
		const int r = rtlsdr_set_center_freq(device, freq);
		if (r < 0) {
			error_list.push_back(fmt::format("Couldn't set center frequency to {}:{}", label, freq));
		}
		selected_frequency_label = label;
		selected_frequency = freq;
	}
private:
	void SearchGains(void) {
		const int nb_gains = rtlsdr_get_tuner_gains(device, NULL);
		if (nb_gains <= 0) {
			return;
		}
		gain_list.clear();
		static std::vector<int> gains;
		gains.resize(nb_gains);
		rtlsdr_get_tuner_gains(device, gains.data());
		for (int g: gains) {
			gain_list.push_back(static_cast<float>(g) * 0.1f);
		}
	}	
	void UpdateDataAsync(uint8_t* buf, uint32_t len) {
		if (len != block_size) {
			error_list.push_back(fmt::format("Got mismatching buffer size {}!={}", len, block_size));
		}
		auto* data = reinterpret_cast<std::complex<uint8_t>*>(buf);
		const int N = len/sizeof(std::complex<uint8_t>);
		obs_on_data.Notify(data, N);
	}
public:
	static void rtlsdr_callback(uint8_t* buf, uint32_t len, void* ctx) {
		auto* instance = reinterpret_cast<Device*>(ctx);
		instance->UpdateDataAsync(buf, len);
	}
};

class DeviceSelector 
{
private:
	Observable<const std::complex<uint8_t>*, const int> obs_on_data;
	Observable<Device*> obs_on_device_change;

	std::vector<DeviceDescriptor> device_list;
	std::list<std::string> error_list;
	std::unique_ptr<Device> device = NULL;	
	std::mutex mutex_device;
public:
	DeviceSelector() {
		SearchDevices();
	}
	~DeviceSelector() {
		CloseDevice();
	}
	void SearchDevices() {
		device_list.clear();

		const int nb_devices = rtlsdr_get_device_count();
		static char vendor_str[256];
		static char product_str[256];
		static char serial_str[256];
		for (int i = 0; i < nb_devices; i++) {
			rtlsdr_get_device_usb_strings(i, vendor_str, product_str, serial_str);
			device_list.push_back({
				i,
				std::string(vendor_str, 	strnlen(vendor_str, 256)), 
				std::string(product_str, 	strnlen(product_str, 256)),
				std::string(serial_str, 	strnlen(serial_str, 256))
			});
		}
	}
	void SelectDevice(int descriptor_index) {
		if ((descriptor_index < 0) || (descriptor_index >= device_list.size())) {
			error_list.push_back(fmt::format("Device {} out of bounds", descriptor_index));
			return;
		}
		rtlsdr_dev_t* dev = NULL;
		const auto& descriptor = device_list[descriptor_index];
		const auto index = (uint32_t)descriptor_index;
		const auto r = rtlsdr_open(&dev, index);
		if (r < 0) {
			error_list.push_back(fmt::format("Failed to open device: {}", index));
			return;
		}

		auto lock = std::unique_lock(mutex_device);
		device = std::make_unique<Device>(dev, descriptor, 4);

		using namespace std::placeholders;
		obs_on_device_change.Notify(device.get());
		device->OnData().Attach(std::bind(&DeviceSelector::OnRawData, this, _1, _2));
	}
	void CloseDevice() { 
		auto lock = std::unique_lock(mutex_device);
		device = NULL; 
		obs_on_device_change.Notify(NULL);
	}
	Device* GetDevice() { return device.get(); }
	auto& GetDeviceMutex() { return mutex_device; }
	const auto& GetDeviceList() { return device_list; }
	auto& GetErrorList() { return error_list; }
	auto& OnData() { return obs_on_data; }
	auto& OnDeviceChange() { return obs_on_device_change; }
private:
	void OnRawData(const std::complex<uint8_t>* data, const int N) {
		obs_on_data.Notify(data, N);
	}
};

class RadioDependencies: public Basic_Radio_Dependencies 
{
public:
    virtual PCM_Player* Create_PCM_Player(void) {
        return new Win32_PCM_Player();
    }
};

void RenderDeviceSelector(DeviceSelector& app);
void RenderDeviceControls(Device& device);
std::unique_ptr<OFDM_Demod> Init_OFDM_Demodulator(const int transmission_mode);

class App 
{
private:
	RadioDependencies dependencies;
	std::string selected_radio;

	int demodulator_cooldown_max = 10;
	int demodulator_cooldown = 0;
public:
	std::unique_ptr<DeviceSelector> device_selector;
	std::unique_ptr<OFDM_Demod> ofdm_demod;

	typedef std::pair<std::unique_ptr<BasicRadio>, std::unique_ptr<SimpleViewController>> radio_instance_t;
	std::unordered_map<std::string, radio_instance_t> basic_radios;

	std::unique_ptr<DoubleBuffer<std::complex<float>>> rx_double_buffer;
	std::unique_ptr<DoubleBuffer<viterbi_bit_t>> double_buffer;

	std::unique_ptr<std::thread> ofdm_demod_thread;
	std::unique_ptr<std::thread> basic_radio_thread;
public:
	App() {
		device_selector = std::make_unique<DeviceSelector>();
		const int transmission_mode = 1;
		const auto dab_params = get_dab_parameters(transmission_mode);
		ofdm_demod = Init_OFDM_Demodulator(transmission_mode);
		double_buffer = std::make_unique<DoubleBuffer<viterbi_bit_t>>(dab_params.nb_frame_bits);

		// TODO: add resize method to double buffer if the length doesn't match
		rx_double_buffer = std::make_unique<DoubleBuffer<std::complex<float>>>(16384*4/sizeof(std::complex<uint8_t>));

		device_selector->OnDeviceChange().Attach([this](Device* device) {
			if (device == NULL) {
				return;
			}
			using namespace std::placeholders;
			device->OnFrequencyChange().Attach(std::bind(&App::OnFrequencyChange, this, _1, _2));
			device->SetCenterFrequency("9C", block_frequencies.at("9C"));
		});

		device_selector->OnData().Attach([this](const std::complex<uint8_t>* data, const int N) {
			if (demodulator_cooldown > 0) {
				demodulator_cooldown--;
				ofdm_demod->Reset();
				return;
			}

			auto* inactive_buf = rx_double_buffer->AcquireInactiveBuffer();
			if (inactive_buf == NULL) {
				return;
			}
			for (int i = 0; i < N; i++) {
				const float I = static_cast<float>(data[i].real()) - 127.0f;
				const float Q = static_cast<float>(data[i].imag()) - 127.0f;
				inactive_buf[i] = std::complex<float>(I, Q);
			}
			rx_double_buffer->ReleaseInactiveBuffer();

		});

		ofdm_demod_thread = std::make_unique<std::thread>([this]() {
			while (true) {
				auto* active_buf = rx_double_buffer->AcquireActiveBuffer();
				if (active_buf == NULL) {
					return;
				}
				const int N = rx_double_buffer->GetLength();
				ofdm_demod->Process(active_buf, N);
				rx_double_buffer->ReleaseActiveBuffer();
			}
		});

		ofdm_demod->On_OFDM_Frame().Attach([this](const viterbi_bit_t* buf, const int nb_carriers, const int nb_symbols) {
			auto* inactive_buf = double_buffer->AcquireInactiveBuffer();		
			if (inactive_buf == NULL) {
				return;
			}
			const int N = double_buffer->GetLength();
			for (int i = 0; i < N; i++) {
				inactive_buf[i] = buf[i];
			}
			double_buffer->ReleaseInactiveBuffer();
		});

		basic_radio_thread = std::make_unique<std::thread>([this]() {
			while (true) {
				auto* active_buf = double_buffer->AcquireActiveBuffer();
				if (active_buf == NULL) {
					return;
				}
				const int N = double_buffer->GetLength();
				auto instance = GetSelectedRadio();
				auto* basic_radio = instance.first;
				if ((basic_radio != NULL) && (demodulator_cooldown == 0)) {
					basic_radio->Process(active_buf, N);
				}

				double_buffer->ReleaseActiveBuffer();
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
private:
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
			RenderDeviceSelector(*app.device_selector.get());
			{
				auto* buf = app.rx_double_buffer->AcquireInactiveBuffer();
				if (buf != NULL) {
					const int N = app.rx_double_buffer->GetLength();
					RenderSourceBuffer(buf, N);
				}
			}
			RenderOFDMDemodulator(app.ofdm_demod.get());
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

void RenderDeviceSelector(DeviceSelector& app) {
	if (ImGui::Begin("Controls")) {
		if (ImGui::Button("Search")) {
			app.SearchDevices();
		}

		auto* selected_device = app.GetDevice();

		std::string preview_label;
		if (selected_device == NULL) {
			preview_label = "None";
		} else {
			const auto& descriptor = selected_device->GetDescriptor();
			preview_label = fmt::format("[{}] {}",
				descriptor.index, descriptor.product);
		}
		if (ImGui::BeginCombo("Devices", preview_label.c_str())) {
			for (auto& device: app.GetDeviceList()) {
				auto label = fmt::format("[{}] Vendor={} Product={} Serial={}",
					device.index, device.vendor, device.product, device.serial);
				const bool is_selected = (selected_device == NULL) ? 
					false : (selected_device->GetDescriptor().index == device.index);

				if (ImGui::Selectable(label.c_str(), is_selected)) {
					if (is_selected) {
						app.CloseDevice();
					} else {
						app.SelectDevice(device.index);
					}
				}
				if (is_selected) {
					ImGui::SetItemDefaultFocus();
				}
			}
			ImGui::EndCombo();
		}

		{
			auto lock = std::unique_lock(app.GetDeviceMutex());
			selected_device = app.GetDevice();
			if (selected_device != NULL) {
				RenderDeviceControls(*selected_device);
			}
		}
	}
	ImGui::End();
}

void RenderDeviceControls(Device& device) {
	std::string preview_label;
	if (!device.GetIsGainManual()) {
		preview_label = "Automatic";
	} else {
		preview_label = fmt::format("{:.1f}dB", device.GetSelectedGain());
	}

	if (ImGui::BeginCombo("Gains", preview_label.c_str())) {
		if (ImGui::Selectable("Automatic", !device.GetIsGainManual())) {
			device.SetAutoGain();
		}
		for (auto gain: device.GetGainList()) {
			const auto label_str = fmt::format("{:.1f}dB", gain);
			const bool is_selected = 
				device.GetIsGainManual() && (device.GetSelectedGain() == gain);
			if (ImGui::Selectable(label_str.c_str(), is_selected)) {
				device.SetGain(gain);
			}
			if (is_selected) {
				ImGui::SetItemDefaultFocus();
			}
		}
		ImGui::EndCombo();
	}

	preview_label = fmt::format("{}:\t{:.3f}", 
		device.GetSelectedFrequencyLabel(), 
		static_cast<float>(device.GetSelectedFrequency())*1e-6f);

	if (ImGui::BeginCombo("Frequencies", preview_label.c_str())) {
		for (auto& [label, value]: block_frequencies) {
			const bool is_selected = (value == device.GetSelectedFrequency());
			const auto label_str = fmt::format("{}:\t{:.3f}", 
				label, static_cast<float>(value)*1e-6f);
			if (ImGui::Selectable(label_str.c_str(), is_selected)) {
				device.SetCenterFrequency(label, value);
			}
			if (is_selected) {
				ImGui::SetItemDefaultFocus();
			}
		}
		ImGui::EndCombo();
	}

	auto& errors = device.GetErrorList();
	if (ImGui::BeginListBox("###Errors")) {
		for (auto& error: errors) {
			ImGui::Selectable(error.c_str());
		}
		ImGui::EndListBox();
	}
}

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