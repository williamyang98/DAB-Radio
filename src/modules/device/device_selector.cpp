#include "device_selector.h"

#include <fmt/core.h>

extern "C" {
#include <rtl-sdr.h>
}

DeviceSelector::DeviceSelector() {
    SearchDevices();
}

DeviceSelector::~DeviceSelector() {
    CloseDevice();
}

void DeviceSelector::SearchDevices() {
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

void DeviceSelector::SelectDevice(int descriptor_index) {
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
}

void DeviceSelector::CloseDevice() { 
    auto lock = std::unique_lock(mutex_device);
    device = NULL; 
    obs_on_device_change.Notify(NULL);
}