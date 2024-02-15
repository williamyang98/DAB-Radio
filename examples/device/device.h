#pragma once

#include <list>
#include <memory>
#include <mutex>
#include <stdint.h>
#include <string>
#include <thread>
#include <vector>
#include <functional>
#include "utility/span.h"

struct DeviceDescriptor {
    std::string vendor;
    std::string product;
    std::string serial;
};

class Device 
{
private:
    DeviceDescriptor m_descriptor;
    struct rtlsdr_dev* m_device;
    const int m_block_size;
    bool m_is_running;
    std::unique_ptr<std::thread> m_runner_thread;

    std::vector<float> m_gain_list;
    bool m_is_gain_manual;
    float m_selected_gain;
    uint32_t m_selected_frequency;
    std::string m_selected_frequency_label;
    std::list<std::string> m_error_list;
    std::function<size_t(tcb::span<const uint8_t>)> m_callback_on_data = nullptr;
    std::function<void(const std::string&, const uint32_t)> m_callback_on_center_frequency = nullptr;
public:
    explicit Device(struct rtlsdr_dev* device, const DeviceDescriptor& descriptor, const int block_size=8192);
    ~Device();
    // we are holding a pointer to rtlsdr_dev_t, so we cant move/copy this class
    Device(Device&) = delete;
    Device(Device&&) = delete;
    Device& operator=(Device&) = delete;
    Device& operator=(Device&&) = delete;
    void Close();
    bool IsRunning() const { return m_is_running; }
    const auto& GetDescriptor() { return m_descriptor; }
    int GetBlockSize(void) { return m_block_size; }
    const auto& GetGainList(void) { return m_gain_list; }
    bool GetIsGainManual(void) { return m_is_gain_manual; }
    float GetSelectedGain(void) { return m_selected_gain; }
    uint32_t GetSelectedFrequency(void) { return m_selected_frequency; }
    const auto& GetSelectedFrequencyLabel(void) { return m_selected_frequency_label; }
    auto& GetErrorList(void) { return m_error_list; }
    void SetAutoGain(void); 
    void SetNearestGain(const float target_gain);
    void SetGain(const float gain);
    void SetSamplingFrequency(const uint32_t freq); 
    void SetCenterFrequency(const uint32_t freq); 
    void SetCenterFrequency(const std::string& label, const uint32_t freq); 
    template <typename F> 
    void SetDataCallback(F&& func) { 
        m_callback_on_data = std::move(func); 
    }
    template <typename F> 
    void SetFrequencyChangeCallback(F&& func) { 
        m_callback_on_center_frequency = std::move(func); 
    }
private:
    void SearchGains(void);
    void OnData(tcb::span<const uint8_t> buf);
    static void rtlsdr_callback(uint8_t* buf, uint32_t len, void* ctx); 
};