#pragma once

#include <stdint.h>

struct NeAACDecFrameInfo;

// Wrapper around libfaad2
class AAC_Decoder 
{
public:
    struct Result {
        uint8_t* audio_buf;
        int nb_audio_buf_bytes;
        bool is_error;
        int error_code;
    };
    struct Params {
        uint32_t sampling_frequency;
        bool is_SBR;
        bool is_stereo;
        bool is_PS;
    };
private:
    const struct Params params;
    // constants
    // libfaad objects
    uint8_t* mp4_bitfile_config;
    int nb_mp4_bitfile_config_bytes = 0;
    void* decoder_handle;
    struct NeAACDecFrameInfo* decoder_frame_info;
public:
    AAC_Decoder(const struct Params _params);
    ~AAC_Decoder();
    Result DecodeFrame(uint8_t* data, const int N);
    Params GetParams() { return params; }
private:
    void GenerateBitfileConfig();
};