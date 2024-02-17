#pragma once
#include <stdint.h>
#include <vector>
#include <string>
#include "utility/span.h"

typedef uint16_t mot_transport_id_t;

struct MOT_Header_Extension_Parameter {
    uint8_t type;
    std::vector<uint8_t> data;
};

struct MOT_UTC_Time {
    bool exists = false;
    int year = 0; 
    int month = 0; 
    int day = 0;
    uint8_t hours = 0;
    uint8_t minutes = 0;
    uint8_t seconds = 0;
    uint16_t milliseconds = 0;
};

struct MOT_Header_Entity {
    uint32_t body_size = 0;
    uint16_t header_size = 0;
    uint8_t content_type = 0;
    uint16_t content_sub_type = 0;

    struct {
        bool exists = false;
        uint8_t charset = 0;
        std::string name;
    } content_name;

    MOT_UTC_Time trigger_time;
    MOT_UTC_Time expire_time;

    std::vector<MOT_Header_Extension_Parameter> user_app_params;
};

struct MOT_Entity {
    mot_transport_id_t transport_id;
    MOT_Header_Entity header;
    tcb::span<const uint8_t> body_buf;
};