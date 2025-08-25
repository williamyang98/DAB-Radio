#pragma once
#include <stdint.h>
#include <optional>
#include <vector>
#include "utility/span.h"

typedef struct mpg123_handle_struct mpg123_handle;

// Wrapper around mpg123
class MP2_Audio_Decoder
{
public:
    enum class MPEG_Version {
        MPEG_1_0,
        MPEG_2_0,
        MPEG_2_5,
        UNKNOWN,
    };
    enum class MPEG_Layer {
        LAYER_I,
        LAYER_II,
        LAYER_III,
        UNKNOWN,
    };
    struct FrameHeader {
        MPEG_Version mpeg_version = MPEG_Version::UNKNOWN;
        MPEG_Layer mpeg_layer = MPEG_Layer::UNKNOWN;
        bool is_stereo = false;
        size_t sample_rate = 0;
        size_t bitrate_kbps = 0;
    };
    struct DecodeResult {
        FrameHeader frame_header;
        tcb::span<const int16_t> audio_data;
        tcb::span<const uint8_t> xpad_data;
        tcb::span<const uint8_t> fpad_data;
    };
private:
    mpg123_handle* m_handle;
public:
    MP2_Audio_Decoder();
    ~MP2_Audio_Decoder();
    MP2_Audio_Decoder(const MP2_Audio_Decoder&) = delete;
    MP2_Audio_Decoder(MP2_Audio_Decoder&&) = delete;
    MP2_Audio_Decoder& operator=(const MP2_Audio_Decoder&) = delete;
    MP2_Audio_Decoder& operator=(MP2_Audio_Decoder&&) = delete;
    std::optional<DecodeResult> decode_frame(tcb::span<const uint8_t> buf);
};
