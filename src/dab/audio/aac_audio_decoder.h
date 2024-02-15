#pragma once

#include <stdint.h>
#include <vector>
#include "utility/span.h"

struct NeAACDecFrameInfo;

// Wrapper around libfaad2
// Consumes AAC access units
// Outputs 16bit stereo audio data
class AAC_Audio_Decoder 
{
public:
    struct Result {
        tcb::span<const uint8_t> audio_buf;
        bool is_error;
        int error_code;
    };
    struct Params {
        uint32_t sampling_frequency;
        bool is_SBR;
        bool is_stereo;
        bool is_PS;
        bool operator==(const Params& other) const {
            return (sampling_frequency == other.sampling_frequency) &&
                   (is_SBR == other.is_SBR) &&
                   (is_stereo == other.is_stereo) &&
                   (is_PS == other.is_PS);
        }
        bool operator!=(const Params& other) const {
            return (sampling_frequency != other.sampling_frequency) || 
                   (is_SBR != other.is_SBR) || 
                   (is_stereo != other.is_stereo) ||
                   (is_PS != other.is_PS);
        }
    };
private:
    const struct Params m_params;
    // constants
    // libfaad objects
    std::vector<uint8_t> m_mp4_bitfile_config;
    void* m_decoder_handle;
    struct NeAACDecFrameInfo* m_decoder_frame_info;
public:
    AAC_Audio_Decoder(const struct Params _params);
    ~AAC_Audio_Decoder();
    AAC_Audio_Decoder(AAC_Audio_Decoder&) = delete;
    AAC_Audio_Decoder(AAC_Audio_Decoder&&) = delete;
    AAC_Audio_Decoder& operator=(AAC_Audio_Decoder&) = delete;
    AAC_Audio_Decoder& operator=(AAC_Audio_Decoder&&) = delete;
    Result DecodeFrame(tcb::span<uint8_t> data);
    Params GetParams() { return m_params; }
private:
    void GenerateBitfileConfig();
};