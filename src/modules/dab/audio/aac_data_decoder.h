#pragma once

#include <stdint.h>
#include "pad/pad_processor.h"

// The AAC codec has a data_stream_element()
// This contains PAD (programma associated data)
class AAC_Data_Decoder 
{
private:
    PAD_Processor pad_processor;
public:
    bool ProcessAccessUnit(const uint8_t* data, const int N);
    auto& Get_PAD_Processor(void) { return pad_processor; }
private:
    bool ProcessDataElement(const uint8_t* data, const int N);
    void ProcessPAD(const uint8_t* fpad, const uint8_t* xpad, const int N);
};