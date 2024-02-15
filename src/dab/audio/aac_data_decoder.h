#pragma once

#include <stdint.h>
#include "../pad/pad_processor.h"
#include "utility/span.h"

// The AAC access unit has a data_stream_element()
// This contains PAD (programme associated data) which we process
class AAC_Data_Decoder 
{
private:
    PAD_Processor m_pad_processor;
public:
    bool ProcessAccessUnit(tcb::span<const uint8_t> data);
    auto& Get_PAD_Processor(void) { return m_pad_processor; }
private:
    bool ProcessDataElement(tcb::span<const uint8_t> data);
    void ProcessPAD(tcb::span<const uint8_t> fpad, tcb::span<const uint8_t> xpad);
};