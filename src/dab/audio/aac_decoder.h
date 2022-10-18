#pragma once

#include <stdint.h>

struct NeAACDecFrameInfo;

// Wrapper around libfaad2
class AAC_Decoder 
{
private:
    // constants
    const uint32_t sampling_frequency;
    const bool is_SBR;
    const bool is_stereo;
    const bool is_PS;
    // libfaad objects
    uint8_t* mp4_bitfile_config;
    int nb_mp4_bitfile_config_bytes = 0;
    void* decoder_handle;
    struct NeAACDecFrameInfo* decoder_frame_info;
public:
    AAC_Decoder(
        const uint32_t _sampling_frequency, 
        const bool _is_SBR, const bool _is_stereo, const bool _is_PS);
    ~AAC_Decoder();
    int DecodeFrame(uint8_t* data, const int N);
private:
    void GenerateBitfileConfig();
};