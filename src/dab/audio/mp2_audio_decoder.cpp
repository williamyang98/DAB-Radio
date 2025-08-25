#include "./mp2_audio_decoder.h"
#include <mpg123.h>
#include <stdexcept>
#include <fmt/core.h>

MP2_Audio_Decoder::MP2_Audio_Decoder() {
    int res = 0;
    m_handle = mpg123_new(nullptr, &res);
    if (m_handle == nullptr) {
        const char* error_string = mpg123_plain_strerror(res);
        throw std::runtime_error(fmt::format("mpg123_new failed with: {}", error_string));
    }

    res = mpg123_open_feed(m_handle);
    if (res != MPG123_OK) {
        const char* error_string = mpg123_plain_strerror(res);
        throw std::runtime_error(fmt::format("mpg123_open_feed failed with: {}", error_string));
    }
    mpg123_format2(
        m_handle,
        0, // handle all rates
        MPG123_STEREO | MPG123_MONO,
        MPG123_ENC_SIGNED_16
    );
#if NDEBUG
    mpg123_param(m_handle, MPG123_ADD_FLAGS, MPG123_QUIET, 0.0);
#endif
}

// DOC: ETSI TS 103 466
// Clause 5.3.2: DAB Audio bit stream
std::optional<MP2_Audio_Decoder::DecodeResult> MP2_Audio_Decoder::decode_frame(tcb::span<const uint8_t> buf) {
    // decode dab frame which is compatible as a mpeg audio frame
    // since we get complete frames each time just reset seek head
    int res = mpg123_feed(m_handle, buf.data(), buf.size());
    if (res != MPG123_OK) {
        return std::nullopt;
    }
    off_t frame_offset = 0;
    uint8_t* audio_data = nullptr;
    size_t audio_bytes = 0;
    res = mpg123_decode_frame(m_handle, &frame_offset, &audio_data, &audio_bytes);
    if (res != MPG123_OK) {
        return std::nullopt;
    }
    const auto audio_buf = tcb::span<const int16_t>(
        reinterpret_cast<const int16_t*>(audio_data),
        audio_bytes/sizeof(int16_t)
    );

    // get frame header
    mpg123_frameinfo2 info;
    res = mpg123_info2(m_handle, &info);
    if (res != MPG123_OK) {
        return std::nullopt;
    }
    FrameHeader frame_header;
    switch (info.version) {
    case MPG123_1_0: frame_header.mpeg_version = MPEG_Version::MPEG_1_0; break;
    case MPG123_2_0: frame_header.mpeg_version = MPEG_Version::MPEG_2_0; break;
    case MPG123_2_5: frame_header.mpeg_version = MPEG_Version::MPEG_2_5; break;
    default:         frame_header.mpeg_version = MPEG_Version::UNKNOWN; break;
    }

    switch (info.layer) {
    case 1:  frame_header.mpeg_layer = MPEG_Layer::LAYER_I; break;
    case 2:  frame_header.mpeg_layer = MPEG_Layer::LAYER_II; break;
    case 3:  frame_header.mpeg_layer = MPEG_Layer::LAYER_III; break;
    default: frame_header.mpeg_layer = MPEG_Layer::UNKNOWN; break;
    }

    switch (info.mode) {
    case MPG123_M_JOINT:  frame_header.is_stereo = true; break;
    case MPG123_M_STEREO: frame_header.is_stereo = true; break;
    case MPG123_M_DUAL:   frame_header.is_stereo = true; break;
    case MPG123_M_MONO:   frame_header.is_stereo = false; break;
    default:              frame_header.is_stereo = false; break;
    }
    const size_t total_channels = frame_header.is_stereo ? 2 : 1;
    frame_header.sample_rate = size_t(info.rate);
    frame_header.bitrate_kbps = size_t(info.bitrate);

    // Figure 5: Structure of the DAB audio frame
    constexpr size_t fpad_size = 2;
    const auto fpad_data = buf.last(fpad_size);

    // Clause B.3 - CRC check for scale factors
    size_t total_scale_factor_crc_bytes = 4;
    if (
        frame_header.sample_rate == 48000 &&
        frame_header.mpeg_version == MPEG_Version::MPEG_1_0 &&
        frame_header.mpeg_layer == MPEG_Layer::LAYER_II
    ) {
        const size_t bitrate_per_channel = frame_header.bitrate_kbps/total_channels;
        if (bitrate_per_channel < 56) {
            total_scale_factor_crc_bytes = 2;
        }
    }
    // determine location of XPAD/FPAD
    constexpr size_t max_xpad_bytes = 196;
    auto xpad_data = buf.first(buf.size()-fpad_size-total_scale_factor_crc_bytes);
    if (xpad_data.size() > max_xpad_bytes) {
        xpad_data = xpad_data.last(max_xpad_bytes);
    }

    // rewind decoder to prevent appending
    // mpg123_feedseek(m_handle, 0, SEEK_SET, nullptr);
    // mpg123_seek_frame(m_handle, 0, SEEK_SET);

    return MP2_Audio_Decoder::DecodeResult {
        frame_header,
        audio_buf,
        xpad_data,
        fpad_data,
    };
}

MP2_Audio_Decoder::~MP2_Audio_Decoder() {
    mpg123_delete(m_handle);
}
