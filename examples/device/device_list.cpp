#include "./device_list.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern "C" {
#include <rtl-sdr.h>
}

#define LOG_ERROR(...) fprintf(stderr, "[device-list] " __VA_ARGS__)

void DeviceList::refresh() {
    const int total_devices = rtlsdr_get_device_count();
    if (total_devices <= 0) {
        {
            auto lock = std::unique_lock(m_mutex_descriptors);
            m_descriptors.clear();
        }
        {
            auto lock = std::unique_lock(m_mutex_errors);
            if (total_devices == 0) {
                LOG_ERROR("No devices were found");
            } else {
                LOG_ERROR("Failed to fetch devices (%d)", total_devices);
            }
        }
        return;
    }

    auto descriptors = std::vector<DeviceDescriptor>(size_t(total_devices));
    constexpr size_t N = 256;
    static char vendor_str[N] = {0};
    static char product_str[N] = {0};
    static char serial_str[N] = {0};
    for (int i = 0; i < total_devices; i++) {
        rtlsdr_get_device_usb_strings(i, vendor_str, product_str, serial_str);
        descriptors[i] = DeviceDescriptor {
            std::string(vendor_str, strnlen(vendor_str, N)), 
            std::string(product_str, strnlen(product_str, N)),
            std::string(serial_str, strnlen(serial_str, N))
        };
    }

    auto lock = std::unique_lock(m_mutex_descriptors);
    m_descriptors = descriptors;
}

std::shared_ptr<Device> DeviceList::get_device(size_t index) {
    if (index >= m_descriptors.size()) {
        auto lock = std::unique_lock(m_mutex_errors);
        LOG_ERROR("Device at index %zu out of bounds", index);
        return nullptr;
    }
 
    auto lock_descriptors = std::unique_lock(m_mutex_descriptors);
    const auto descriptor = m_descriptors[index];
    lock_descriptors.unlock();

    rtlsdr_dev_t* device = nullptr;
    const auto status = rtlsdr_open(&device, uint32_t(index));
    if (status < 0) {
        auto lock = std::unique_lock(m_mutex_errors);
        LOG_ERROR("Failed to open device at index %zu (%d)", index, status);
        return nullptr;
    }
    return std::make_shared<Device>(device, descriptor, 4);
}