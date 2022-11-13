#pragma once

#include <map>
#include <string>
#include <stdint.h>

class DeviceSelector;

typedef std::map<std::string, uint32_t> block_frequency_table_t;
void RenderDeviceSelector(DeviceSelector& app, const block_frequency_table_t& block_frequencies);