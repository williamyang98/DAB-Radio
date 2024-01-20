#pragma once
#include <stdint.h>

// NOTE: This extra information that is given that we dont' really have a need for

struct DAB_CIF_Counter {
    uint8_t upper_count = 0; // Goes up to 20
    uint8_t lower_count = 0; // Goes up to 250
    uint16_t GetTotalCount() const {
        return 
            static_cast<uint16_t>(upper_count)*250u + 
            static_cast<uint16_t>(lower_count);
    }
};

struct DAB_Datetime {
    int day = 0;
    int month = 0;
    int year = 0;
    uint8_t hours = 0;
    uint8_t minutes = 0;
    uint8_t seconds = 0;
    uint16_t milliseconds = 0;
};

struct DAB_Misc_Info {
    DAB_Datetime datetime;
    DAB_CIF_Counter cif_counter;
};