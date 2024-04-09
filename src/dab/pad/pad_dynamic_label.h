#pragma once

#include <stddef.h>
#include <stdint.h>
#include <memory>
#include <string_view>
#include "utility/observable.h"
#include "utility/span.h"
#include "./pad_data_group.h"

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
    enum class Command: uint8_t {
        CLEAR
    };
private:
    enum class GroupType { LABEL_SEGMENT, COMMAND };
    enum class State { WAIT_START, READ_LENGTH, READ_DATA };
private:
    PAD_Data_Group m_data_group;
    State m_state;
    GroupType m_group_type;
    std::unique_ptr<PAD_Dynamic_Label_Assembler> m_assembler;
    uint8_t m_previous_toggle_flag;
    // label_buffer, charset
    Observable<std::string_view, const uint8_t> m_obs_on_label_change;
    Observable<uint8_t> m_obs_on_command;
public:
    PAD_Dynamic_Label();
    ~PAD_Dynamic_Label();
    void ProcessXPAD(const bool is_start, tcb::span<const uint8_t> buf);
    auto& OnLabelChange(void) { return m_obs_on_label_change; }
    auto& OnCommand(void) { return m_obs_on_command; }
private:
    size_t ConsumeBuffer(const bool is_start, tcb::span<const uint8_t> buf);
    void ReadGroupHeader(void);
    void InterpretLabelSegment(void);
    void InterpretCommand(void);
};
