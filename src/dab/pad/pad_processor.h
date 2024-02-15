#pragma once

#include <stdint.h>
#include <vector>
#include <memory>
#include <string_view>
#include "../mot/MOT_entities.h"
#include "utility/observable.h"
#include "utility/span.h"

class PAD_Data_Length_Indicator;
class PAD_Dynamic_Label;
class PAD_MOT_Processor;

struct PAD_Content_Indicator {
    uint8_t length;
    uint8_t app_type;
};

// Takes in PAD information and decodes into into relevant objects
// Updated/new entities are signalled through the observer callbacks
class PAD_Processor 
{
private:
    // The incoming XPAD field has reversed byte order which we unreverse
    std::vector<uint8_t> m_xpad_unreverse_buf;
    std::vector<PAD_Content_Indicator> m_ci_list;

    std::unique_ptr<PAD_Data_Length_Indicator> m_data_length_indicator;
    std::unique_ptr<PAD_Dynamic_Label> m_dynamic_label;
    std::unique_ptr<PAD_MOT_Processor> m_pad_mot_processor;

    // We associated MOT XPAD lengths to the most recently declared data length indicator
    uint16_t m_previous_mot_length;
public:
    PAD_Processor();
    ~PAD_Processor();
    void Process(tcb::span<const uint8_t> fpad, tcb::span<const uint8_t> xpad_reversed);

    // label, charset
    Observable<std::string_view, const uint8_t>& OnLabelUpdate();
    // command id
    Observable<uint8_t>& OnLabelCommand();
    // mot object
    Observable<MOT_Entity>& OnMOTUpdate();
private:
    void Process_Short_XPAD(tcb::span<const uint8_t> xpad, const bool has_indicator_list);
    void Process_Variable_XPAD(tcb::span<const uint8_t> xpad, const bool has_indicator_list);
    void ProcessDataField(tcb::span<const uint8_t> data_field);
};