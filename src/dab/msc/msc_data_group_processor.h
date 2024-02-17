#pragma once

#include <stdint.h>
#include "utility/span.h"

// Decodes the data group sent over MSC (main service component)
struct MSC_Data_Group_Process_Result {
    enum class Status {
        SUCCESS, 
        SHORT_GROUP_HEADER,
        SHORT_CRC_FIELD,
        CRC_INVALID, 
        SHORT_EXTENSION_FIELD,
        SHORT_SEGMENT_FIELD,
        SHORT_SESSION_HEADER,
        SHORT_ACCESS_FIELD_HEADER,
        SHORT_ACCESS_FIELDS,
        SHORT_TRANSPORT_ID_FIELD,
        OVERFLOW_MAX_DATA_FIELD_SIZE,
    };
    Status status = Status::SUCCESS;
    // header flags
    bool has_header_fields = false;
    uint8_t data_group_type = 0;
    uint8_t continuity_index = 0;
    uint8_t repetition_index = 0;
    // crc check
    bool has_crc = false;
    uint16_t crc_rx = 0;
    uint16_t crc_calc = 0;
    // extension
    bool has_extension_field = false;
    uint16_t extension_field = 0;
    // segment field
    bool has_segment_field = false;
    struct {
        bool is_last_segment = false;
        uint16_t segment_number = 0;
    } segment_field;
    // user access fields
    bool has_transport_id = false;
    uint16_t transport_id = 0;
    bool has_user_access_fields = false;
    tcb::span<const uint8_t> user_access_fields = {};
    // data fields
    tcb::span<const uint8_t> data_field;
};

MSC_Data_Group_Process_Result MSC_Data_Group_Process(tcb::span<const uint8_t> data_group);