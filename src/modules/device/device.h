#pragma once

#include <stdint.h>
#include <string>
#include <vector>
#include <list>
#include <memory>
#include <complex>

#include <thread>
#include <mutex>

#include "utility/observable.h"
#include "utility/span.h"

extern "C" {
#include <rtl-sdr.h>
}

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
	const int total_samples;
	const int total_bytes;

	std::vector<float> gain_list;
	bool is_gain_manual;
	float selected_gain;
	uint32_t selected_frequency;
	std::string selected_frequency_label;
	std::unique_ptr<std::thread> runner_thread;
	std::list<std::string> error_list;

	Observable<tcb::span<const std::complex<uint8_t>>> obs_on_data;
	Observable<const std::string&, const uint32_t> obs_on_center_frequency;
public:
	Device(rtlsdr_dev_t* _device, const DeviceDescriptor& _descriptor, const int block_multiple=1);
	~Device();
	// we are holding a pointer to rtlsdr_dev_t, so we cant move/copy this class
	Device(Device&) = delete;
	Device(Device&&) = delete;
	Device& operator=(Device&) = delete;
	Device& operator=(Device&&) = delete;
public:
	const auto& GetDescriptor() { return descriptor; }
	const int GetTotalSamples(void) { return total_samples; }
	const int GetTotalBytes(void) { return total_bytes; }
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
	void SetAutoGain(void); 
	void SetNearestGain(const float target_gain);
	void SetGain(const float gain);
	void SetSamplingFrequency(const uint32_t freq); 
	void SetCenterFrequency(const uint32_t freq); 
	void SetCenterFrequency(const std::string& label, const uint32_t freq); 
private:
	void SearchGains(void);	
	void UpdateDataAsync(tcb::span<uint8_t> buf); 
private:
	static void rtlsdr_callback(uint8_t* buf, uint32_t len, void* ctx); 
};