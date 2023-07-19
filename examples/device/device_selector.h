#pragma once

#include <stdint.h>
#include <complex>
#include <list>
#include <vector>
#include <mutex>
#include <memory>

#include "./device.h"
#include "utility/observable.h"

class DeviceSelector 
{
private:
    Observable<Device*> obs_on_device_change;

    std::vector<DeviceDescriptor> device_list;
    std::list<std::string> error_list;
    std::unique_ptr<Device> device = NULL;    
    std::mutex mutex_device;
public:
    DeviceSelector(); 
    ~DeviceSelector(); 
    void SearchDevices(); 
    void SelectDevice(int descriptor_index);
    void CloseDevice(); 
    Device* GetDevice() { return device.get(); }
    auto& GetDeviceMutex() { return mutex_device; }
    const auto& GetDeviceList() { return device_list; }
    auto& GetErrorList() { return error_list; }
    auto& OnDeviceChange() { return obs_on_device_change; }
};