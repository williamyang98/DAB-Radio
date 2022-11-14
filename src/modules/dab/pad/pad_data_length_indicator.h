#pragma once

#include "pad_data_group.h"

// Create data length indicator from XPAD segments
class PAD_Data_Length_Indicator 
{
private:
    PAD_Data_Group data_group;    
    uint16_t length;
    bool is_length_available;
public:
    PAD_Data_Length_Indicator();
    void ProcessXPAD(tcb::span<const uint8_t> buf);
    uint16_t GetLength(void) const { return length; }
    bool GetIsLengthAvailable(void) const { return is_length_available; }
    void ResetLength(void);
private:
    size_t Consume(tcb::span<const uint8_t> buf);
    void Interpret(void);
};