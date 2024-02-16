#pragma once
#include <stdint.h>

class Basic_Audio_Controls 
{
private:
    uint8_t flags = 0;
public:
    // Is anything enabled?
    bool GetAnyEnabled(void) const;
    bool GetAllEnabled(void) const;
    void RunAll(void);
    void StopAll(void);
    // Decode AAC audio elements
    bool GetIsDecodeAudio(void) const;
    void SetIsDecodeAudio(bool);
    // Decode AAC data_stream_element
    bool GetIsDecodeData(void) const;
    void SetIsDecodeData(bool);
    // Play audio data through sound device
    bool GetIsPlayAudio(void) const;
    void SetIsPlayAudio(bool);
private:
    void SetFlag(const uint8_t flag, const bool state);
};

