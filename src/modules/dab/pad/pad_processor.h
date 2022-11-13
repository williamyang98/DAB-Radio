#pragma once

#include <stdint.h>
#include "../mot/MOT_processor.h"
#include "pad_dynamic_label.h"
#include "pad_MOT_processor.h"
#include "utility/observable.h"

class PAD_Data_Length_Indicator;

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
    uint8_t* xpad_unreverse_buf;

    PAD_Content_Indicator* ci_list;
    int ci_list_length;

    PAD_Data_Length_Indicator* data_length_indicator;
    PAD_Dynamic_Label* dynamic_label;
    PAD_MOT_Processor* pad_mot_processor;

    // We associated MOT XPAD lengths to the most recently declared data length indicator
    uint16_t previous_mot_length;
public:
    PAD_Processor();
    ~PAD_Processor();
    void Process(const uint8_t* fpad, const uint8_t* xpad_reversed, const int nb_xpad_bytes);
    auto& OnLabelUpdate(void) { return dynamic_label->OnLabelChange(); }
    auto& OnLabelCommand(void) { return dynamic_label->OnCommand(); }
    auto& OnMOTUpdate(void) { return pad_mot_processor->Get_MOT_Processor()->OnEntityComplete(); }
private:
    void Process_Short_XPAD(const uint8_t* xpad, const int N, const bool has_indicator_list);
    void Process_Variable_XPAD(const uint8_t* xpad, const int N, const bool has_indicator_list);
    void ProcessDataField(const uint8_t* data_field, const int N);
};