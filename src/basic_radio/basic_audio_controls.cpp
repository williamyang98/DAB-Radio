#include "./basic_audio_controls.h"
#include <stdint.h>

// controls
constexpr uint8_t CONTROL_FLAG_DECODE_AUDIO = 0b10000000;
constexpr uint8_t CONTROL_FLAG_DECODE_DATA  = 0b01000000;
constexpr uint8_t CONTROL_FLAG_PLAY_AUDIO   = 0b00100000;
constexpr uint8_t CONTROL_FLAG_ALL_SELECTED = 0b11100000;

bool Basic_Audio_Controls::GetAnyEnabled(void) const {
    return (flags != 0);
}

bool Basic_Audio_Controls::GetAllEnabled(void) const {
    return (flags == CONTROL_FLAG_ALL_SELECTED);
}

void Basic_Audio_Controls::RunAll(void) {
    flags = CONTROL_FLAG_ALL_SELECTED;
}

void Basic_Audio_Controls::StopAll(void) {
    flags = 0;
}

// Decode AAC audio elements
bool Basic_Audio_Controls::GetIsDecodeAudio(void) const {
    return (flags & CONTROL_FLAG_DECODE_AUDIO) != 0;
}

void Basic_Audio_Controls::SetIsDecodeAudio(bool v) {
    SetFlag(CONTROL_FLAG_DECODE_AUDIO, v);
    if (!v) {
        SetFlag(CONTROL_FLAG_PLAY_AUDIO, false);
    }
}

// Decode AAC data_stream_element
bool Basic_Audio_Controls::GetIsDecodeData(void) const {
    return (flags & CONTROL_FLAG_DECODE_DATA) != 0;
}

void Basic_Audio_Controls::SetIsDecodeData(bool v) {
    SetFlag(CONTROL_FLAG_DECODE_DATA, v);
}

// Play audio data through sound device
bool Basic_Audio_Controls::GetIsPlayAudio(void) const {
    return (flags & CONTROL_FLAG_PLAY_AUDIO) != 0;
}

void Basic_Audio_Controls::SetIsPlayAudio(bool v) { 
    SetFlag(CONTROL_FLAG_PLAY_AUDIO, v);
    if (v) {
        SetFlag(CONTROL_FLAG_DECODE_AUDIO, true);
    }
}

void Basic_Audio_Controls::SetFlag(const uint8_t flag, const bool state) {
    if (state) {
        flags |= flag;
    } else {
        flags &= ~flag;
    }
}
