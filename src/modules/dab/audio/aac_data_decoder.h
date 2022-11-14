#pragma once

#include <stdint.h>
#include "pad/pad_processor.h"
#include "utility/span.h"

// The AAC codec has a data_stream_element()
// This contains PAD (programma associated data)
class AAC_Data_Decoder 
{
private:
    PAD_Processor pad_processor;
public:
    bool ProcessAccessUnit(tcb::span<const uint8_t> data);
    auto& Get_PAD_Processor(void) { return pad_processor; }
private:
    bool ProcessDataElement(tcb::span<const uint8_t> data);
    void ProcessPAD(tcb::span<const uint8_t> fpad, tcb::span<const uint8_t> xpad);
};