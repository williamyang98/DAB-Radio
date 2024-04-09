#pragma once

#include <stddef.h>
#include <stdint.h>
#include <memory>
#include "utility/span.h"
#include "./pad_data_group.h"

class MOT_Processor;

// This class does the following steps: 
// 1. Reconstructs the MSC XPAD data group from XPAD data group segments
// 2. Passes reconstructed MSC XPAD data group to msc_xpad_processor for decoding
// 3. Passes decoded MSC XPAD data group to the MOT processor as a MOT segment
// 4. MOT segments are assembled into MOT entities 
class PAD_MOT_Processor 
{
private:
    enum class State { WAIT_LENGTH, WAIT_START, READ_DATA };
private:
    PAD_Data_Group m_data_group;    
    State m_state;
    std::unique_ptr<MOT_Processor> m_mot_processor;
public:
    PAD_MOT_Processor();
    ~PAD_MOT_Processor();
    void ProcessXPAD(
        const bool is_start, const bool is_conditional_access, 
        tcb::span<const uint8_t> buf);
    void SetGroupLength(const uint16_t length);
    MOT_Processor& Get_MOT_Processor(void) { return *m_mot_processor.get(); }
private:
    size_t Consume(
        const bool is_start, const bool is_conditional_access, 
        tcb::span<const uint8_t> buf);
    void Interpret(void);
};