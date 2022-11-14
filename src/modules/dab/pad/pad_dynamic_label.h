#pragma once

#include <stdint.h>
#include <memory>
#include <string_view>
#include "pad_data_group.h"
#include "utility/observable.h"
#include "utility/span.h"

class PAD_Dynamic_Label_Assembler;

// XPAD data group segments are combined to create:
// 1. Dynamic label
//    Multiple XPAD data group segments creates a single dynamic label segment
//    Multiple dynamic label segments creates a dynamic label
// 2. Command
//    Multiple XPAD data group segments creates a single command
class PAD_Dynamic_Label 
{
public:
    enum Command: uint8_t {
        CLEAR
    };
private:
    enum GroupType { LABEL_SEGMENT, COMMAND };
    enum State { WAIT_START, READ_LENGTH, READ_DATA };
private:
    PAD_Data_Group data_group;
    State state;
    GroupType group_type;
    std::unique_ptr<PAD_Dynamic_Label_Assembler> assembler;
    uint8_t previous_toggle_flag;
    // label_buffer, charset
    Observable<std::string_view, const uint8_t> obs_on_label_change;
    Observable<uint8_t> obs_on_command;
public:
    PAD_Dynamic_Label();
    ~PAD_Dynamic_Label();
    void ProcessXPAD(const bool is_start, tcb::span<const uint8_t> buf);
    auto& OnLabelChange(void) { return obs_on_label_change; }
    auto& OnCommand(void) { return obs_on_command; }
private:
    size_t ConsumeBuffer(const bool is_start, tcb::span<const uint8_t> buf);
    void ReadGroupHeader(void);
    void InterpretLabelSegment(void);
    void InterpretCommand(void);
};
