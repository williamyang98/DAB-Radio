#pragma once

#include <map>
#include <string>
#include <stdint.h>
#include <memory>

class DeviceList;
class Device;

typedef std::map<std::string, uint32_t> block_frequency_table_t;
void RenderDevice(Device& device, const block_frequency_table_t& frequencies);
std::shared_ptr<Device> RenderDeviceList(DeviceList& device_list, Device* device);