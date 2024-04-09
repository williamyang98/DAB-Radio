#include "./device.h"

extern "C" {
#include <rtl-sdr.h>
}

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <cmath>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include <fmt/format.h>
#include "utility/span.h"

Device::Device(rtlsdr_dev_t* device, const DeviceDescriptor& descriptor, const int block_size)
:  m_descriptor(descriptor), m_device(device), m_block_size(block_size)
{
    m_is_running = true;
    m_is_gain_manual = true;
    m_selected_gain = 0.0f;

    SearchGains();
    SetNearestGain(19.0f);
    SetSamplingFrequency(2048000);

    int status = 0;
    status = rtlsdr_set_bias_tee(m_device, 0);
    if (status < 0) m_error_list.push_back(fmt::format("Failed to disable bias tee ({})", status));
    status = rtlsdr_reset_buffer(m_device);
    if (status < 0) m_error_list.push_back(fmt::format("Failed to reset buffer ({})", status));

    m_runner_thread = std::make_unique<std::thread>([this]() {
        const int status_read = rtlsdr_read_async(
            m_device, 
            &Device::rtlsdr_callback, reinterpret_cast<void*>(this), 
            0, m_block_size
        );
        fprintf(stderr, "[device] rtlsdr_read_sync exited with %d\n", status_read);
    });
}

Device::~Device() {
    Close();
    // FIXME: Depending on the USB driver installed the following may occur
    //        1. Segmentation fault in driver
    //        2. Driver goes into an infinite loop
    //        3. runner_thread doesn't exit and destructor is stuck at thread join
    //        4. It works but rtlsdr_read_async returns a negative status code
    m_runner_thread->join();
    rtlsdr_close(m_device);
    m_device = nullptr;
}

void Device::Close() {
    m_is_running = false;
    rtlsdr_cancel_async(m_device);
}

void Device::SetAutoGain(void) {
    const int status = rtlsdr_set_tuner_gain_mode(m_device, 0);
    if (status < 0) {
        m_error_list.push_back(fmt::format("Failed to set tuner gain mode to automatic ({})", status));
        return;
    }
    m_is_gain_manual = false;
    m_selected_gain = 0.0f;
}

void Device::SetNearestGain(const float target_gain) {
    float min_err = 10000.0f;
    float nearest_gain = 0.0f;
    for (const auto& gain: m_gain_list) {
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
    int status = 0;
    status = rtlsdr_set_tuner_gain_mode(m_device, 1);
    if (status < 0) {
        m_error_list.push_back(fmt::format("Failed to set tuner gain mode to manual ({})", status));
        return;
    }
    status = rtlsdr_set_tuner_gain(m_device, qgain);
    if (status < 0) {
        m_error_list.push_back(fmt::format("Failed to set manual gain to {:.1f}dB ({})", gain, status));
        return;
    }
    m_is_gain_manual = true;
    m_selected_gain = gain;
}

void Device::SetSamplingFrequency(const uint32_t freq) {
    const int status = rtlsdr_set_sample_rate(m_device, freq);
    if (status < 0) {
        m_error_list.push_back(fmt::format("Failed to set sampling frequency to {} Hz ({})", freq, status));
        return;
    }
}

void Device::SetCenterFrequency(const uint32_t freq) {
    SetCenterFrequency("Manual", freq);
}

void Device::SetCenterFrequency(const std::string& label, const uint32_t freq) {
    if (m_callback_on_center_frequency != nullptr) {
        m_callback_on_center_frequency(label, freq);
    }
    const int status = rtlsdr_set_center_freq(m_device, freq);
    if (status < 0) {
        m_error_list.push_back(fmt::format("Failed to set center frequency to {}@{}Hz ({})", label, freq, status));
        // Resend notification with original frequency
        if (m_callback_on_center_frequency != nullptr) {
            m_callback_on_center_frequency(m_selected_frequency_label, m_selected_frequency);
        }
        return;
    }
    m_selected_frequency_label = label;
    m_selected_frequency = freq;
}

void Device::SearchGains(void) {
    const int total_gains = rtlsdr_get_tuner_gains(m_device, NULL);
    if (total_gains <= 0) {
        return;
    }
    auto qgains = std::vector<int>(size_t(total_gains));
    m_gain_list.resize(size_t(total_gains));
    rtlsdr_get_tuner_gains(m_device, qgains.data());
    for (size_t i = 0; i < size_t(total_gains); i++) {
        const int qgain = qgains[i];
        const float gain = static_cast<float>(qgain) * 0.1f;
        m_gain_list[i] = gain;
    }
}

void Device::OnData(tcb::span<const uint8_t> buf) {
    if (!m_is_running) return;
    if (m_callback_on_data == nullptr) return;
    const size_t total_bytes = m_callback_on_data(buf);
    if (total_bytes != buf.size()) {
        fprintf(stderr, "Short write, samples lost, %zu/%zu, shutting down device!\n", total_bytes, buf.size());
        Close();
    }
}

void Device::rtlsdr_callback(uint8_t* buf, uint32_t len, void* ctx) {
    auto* device = reinterpret_cast<Device*>(ctx);
    auto data = tcb::span<const uint8_t>(buf, size_t(len));
    device->OnData(data);
}