#include "device.h"

#include <fmt/core.h>

Device::Device(rtlsdr_dev_t* _device, const DeviceDescriptor& _descriptor, const int block_multiple)
: device(_device), descriptor(_descriptor),
  total_samples(16384*block_multiple),
  total_bytes(total_samples*sizeof(std::complex<uint8_t>))
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
            reinterpret_cast<void*>(this), 0, total_bytes);
    });
}

Device::~Device() {
    rtlsdr_cancel_async(device);
    runner_thread->join();
    rtlsdr_close(device);
}

void Device::SetAutoGain(void) {
    int r = rtlsdr_set_tuner_gain_mode(device, 0);
    if (r < 0) {
        error_list.push_back("Couldn't set tuner gain mode to automatic");
        return;
    }
    is_gain_manual = false;
    selected_gain = 0.0f;
}

void Device::SetNearestGain(const float target_gain) {
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

void Device::SetGain(const float gain) {
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

void Device::SetSamplingFrequency(const uint32_t freq) {
    const int r = rtlsdr_set_sample_rate(device, freq);
    if (r < 0) {
        error_list.push_back(fmt::format("Couldn't set sampling frequency to {}", freq));
        return;
    }
}

void Device::SetCenterFrequency(const uint32_t freq) {
    SetCenterFrequency("Manual", freq);
}

void Device::SetCenterFrequency(const std::string& label, const uint32_t freq) {
    obs_on_center_frequency.Notify(label, freq);
    const int r = rtlsdr_set_center_freq(device, freq);
    if (r < 0) {
        error_list.push_back(fmt::format("Couldn't set center frequency to {}:{}", label, freq));
        // Resend notification with original frequency
        obs_on_center_frequency.Notify(selected_frequency_label, selected_frequency);
        return;
    }
    selected_frequency_label = label;
    selected_frequency = freq;
}

void Device::SearchGains(void) {
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

void Device::UpdateDataAsync(tcb::span<uint8_t> buf) {
    const int len = (int)buf.size();
    if (len != total_bytes) {
        error_list.push_back(fmt::format("Got mismatching buffer size {}!={}", len, total_bytes));
    }
    auto* data = reinterpret_cast<std::complex<uint8_t>*>(buf.data());
    obs_on_data.Notify({data, (size_t)total_samples});
}

void Device::rtlsdr_callback(uint8_t* buf, uint32_t len, void* ctx) {
    auto* instance = reinterpret_cast<Device*>(ctx);
    instance->UpdateDataAsync({buf, (size_t)len});
}