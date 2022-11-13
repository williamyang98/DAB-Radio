#pragma once

#include <stdint.h>
#include "pad_data_group.h"
#include "utility/observable.h"

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
    enum Command {
        CLEAR
    };
private:
    enum GroupType { LABEL_SEGMENT, COMMAND };
    enum State { WAIT_START, READ_LENGTH, READ_DATA };
private:
    PAD_Data_Group data_group;
    State state;
    GroupType group_type;
    PAD_Dynamic_Label_Assembler* assembler;
    uint8_t previous_toggle_flag;
    // label_buffer, nb_label_bytes, charset
    Observable<const uint8_t*, const int, const uint8_t> obs_on_label_change;
    Observable<Command> obs_on_command;
public:
    PAD_Dynamic_Label();
    ~PAD_Dynamic_Label();
    void ProcessXPAD(const bool is_start, const uint8_t* buf, const int N);
    auto& OnLabelChange(void) { return obs_on_label_change; }
    auto& OnCommand(void) { return obs_on_command; }
private:
    int ConsumeBuffer(const bool is_start, const uint8_t* buf, const int N);
    void ReadGroupHeader(void);
    void InterpretLabelSegment(void);
    void InterpretCommand(void);
};
