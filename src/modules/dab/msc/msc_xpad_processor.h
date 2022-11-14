#pragma once

#include <stdint.h>
#include "utility/span.h"

// Decodes the XPAD field sent over MSC (main service component)
class MSC_XPAD_Processor 
{
public:
    struct ProcessResult {
        bool is_success = false;

        uint8_t data_group_type = 0;
        uint8_t continuity_index = 0;
        uint8_t repetition_index = 0;

        bool has_extension_field = false;
        uint16_t extension_field = 0;

        bool has_segment_field = false;
        struct {
            bool is_last_segment = false;
            uint16_t segment_number = 0;
        } segment_field;

        bool has_user_access_field = false;
        struct {
            bool has_transport_id = false;
            uint16_t transport_id = 0;

            const uint8_t* end_address = NULL;
            int nb_end_address_bytes = 0;
        } user_access_field;


        tcb::span<const uint8_t> data_field;
    };
public:
    ProcessResult Process(tcb::span<const uint8_t> buf);
};