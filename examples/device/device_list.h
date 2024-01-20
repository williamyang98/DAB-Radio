#pragma once

#include <stdint.h>
#include <list>
#include <memory>
#include <mutex>
#include <vector>

#include "./device.h"
#include "utility/span.h"

class DeviceList 
{
private:
    std::mutex m_mutex_descriptors;
    std::mutex m_mutex_errors;
    std::vector<DeviceDescriptor> m_descriptors;
public:
    auto& get_mutex_descriptors() { return m_mutex_descriptors; }
    tcb::span<const DeviceDescriptor> get_descriptors() const { return m_descriptors; }
    void refresh(); 
    std::shared_ptr<Device> get_device(size_t index);
};