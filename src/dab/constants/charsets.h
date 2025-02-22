#pragma once

#include <stdint.h>
#include <string>
#include "utility/span.h"

std::string convert_charset_to_utf8(tcb::span<const uint8_t> buf, uint8_t charset);
