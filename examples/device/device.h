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
    DeviceDescriptor descriptor;
    struct rtlsdr_dev* device;
    const int block_size;
    bool is_running;
    std::unique_ptr<std::thread> runner_thread;

    std::vector<float> gain_list;
    bool is_gain_manual;
    float selected_gain;
    uint32_t selected_frequency;
    std::string selected_frequency_label;
    std::list<std::string> error_list;
    std::function<size_t(tcb::span<const uint8_t>)> callback_on_data = nullptr;
    std::function<void(const std::string&, const uint32_t)> callback_on_center_frequency = nullptr;
public:
    Device(struct rtlsdr_dev* _device, const DeviceDescriptor& _descriptor, const int _block_size=8192);
    ~Device();
    // we are holding a pointer to rtlsdr_dev_t, so we cant move/copy this class
    Device(Device&) = delete;
    Device(Device&&) = delete;
    Device& operator=(Device&) = delete;
    Device& operator=(Device&&) = delete;
    void Close();
    bool IsRunning() const { return is_running; }
    const auto& GetDescriptor() { return descriptor; }
    int GetBlockSize(void) { return block_size; }
    const auto& GetGainList(void) { return gain_list; }
    bool GetIsGainManual(void) { return is_gain_manual; }
    float GetSelectedGain(void) { return selected_gain; }
    uint32_t GetSelectedFrequency(void) { return selected_frequency; }
    const auto& GetSelectedFrequencyLabel(void) { return selected_frequency_label; }
    auto& GetErrorList(void) { return error_list; }
    void SetAutoGain(void); 
    void SetNearestGain(const float target_gain);
    void SetGain(const float gain);
    void SetSamplingFrequency(const uint32_t freq); 
    void SetCenterFrequency(const uint32_t freq); 
    void SetCenterFrequency(const std::string& label, const uint32_t freq); 
    template <typename F> 
    void SetDataCallback(F&& func) { 
        callback_on_data = std::move(func); 
    }
    template <typename F> 
    void SetFrequencyChangeCallback(F&& func) { 
        callback_on_center_frequency = std::move(func); 
    }
private:
    void SearchGains(void);
    void OnData(tcb::span<const uint8_t> buf);
    static void rtlsdr_callback(uint8_t* buf, uint32_t len, void* ctx); 
};