#pragma once

#include <stdint.h>

// DOC: ETSI TS 101 756
// Clause 6.1: Content type and content subtypes 
// Table 17: Content type and content subtypes 

enum class MOT_Content_Type: uint8_t {
    GENERAL         = 0b000000,
    TEXT            = 0b000001,
    IMAGE           = 0b000010,
    AUDIO           = 0b000011,
    VIDEO           = 0b000100,
    MOT_TRANSPORT   = 0b000101,
    SYSTEM          = 0b000110,
    APPLICATION     = 0b000111,
    PROPRIETARY     = 0b111111,
};

#define TYPE(a,b) static_cast<uint16_t>((static_cast<uint8_t>(a) << 9) | b)

enum class MOT_Content_Subtype: uint16_t {
    GENERAL_DATA_OBJECT_TRANSFER = TYPE(MOT_Content_Type::GENERAL,       0b0000),
    TEXT_HTML                    = TYPE(MOT_Content_Type::TEXT,          0b0010),
    TEXT_PDF                     = TYPE(MOT_Content_Type::TEXT,          0b0011),
    IMAGE_JPEG                   = TYPE(MOT_Content_Type::IMAGE,         0b0001),
    IMAGE_PNG                    = TYPE(MOT_Content_Type::IMAGE,         0b0011),
    AUDIO_MPEG_I_LAYER_II        = TYPE(MOT_Content_Type::AUDIO,         0b0001),
    AUDIO_MPEG_II_LAYER_II       = TYPE(MOT_Content_Type::AUDIO,         0b0100),
    AUDIO_MPEG_4                 = TYPE(MOT_Content_Type::AUDIO,         0b1010),
    VIDEO_MPEG_4                 = TYPE(MOT_Content_Type::VIDEO,         0b0010),
    MOT_HEADER_UPDATE            = TYPE(MOT_Content_Type::MOT_TRANSPORT, 0b0000),
    MOT_HEADER_ONLY              = TYPE(MOT_Content_Type::MOT_TRANSPORT, 0b0001),
};

#undef TYPE

static MOT_Content_Subtype GetMOTContentType(uint8_t type, uint16_t subtype) {
    const uint16_t v = ((type    &    0b111111) << 9) | 
                       ((subtype & 0b111111111) << 0);
    return static_cast<MOT_Content_Subtype>(v);
}
