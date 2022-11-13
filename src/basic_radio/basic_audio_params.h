#pragma once

#include <stdint.h>

struct BasicAudioParams {
    uint32_t frequency;
    uint8_t bytes_per_sample;
    bool is_stereo;
    bool operator==(const BasicAudioParams& other) const {
        return (frequency == other.frequency) &&
               (bytes_per_sample == other.bytes_per_sample) &&
               (is_stereo == other.is_stereo);
    }
    bool operator!=(const BasicAudioParams& other) const {
        return !(*this == other);
    }
};