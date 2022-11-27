#pragma once

#include "utility/span.h"
#include "audio_mixer.h"
#include <mutex>
#include <portaudio.h>

// PortAudio uses callbacks to fetch audio samples
// Thus we need a ring buffer to 
class PortAudio_Output
{
public:
    PaStream* pa_stream;
    PaDeviceIndex pa_selected_device;

    bool is_running;
    std::mutex mutex_pa_stream;

    const int frames_per_block;
    const int sample_rate;
    const int total_channels;

    AudioMixer mixer;
public:
    PortAudio_Output(const int _sample_rate=48000);
    virtual ~PortAudio_Output();
    PortAudio_Output(PortAudio_Output&) = delete;
    PortAudio_Output(PortAudio_Output&&) = delete;
    PortAudio_Output& operator=(PortAudio_Output&) = delete;
    PortAudio_Output& operator=(PortAudio_Output&&) = delete;
    
    auto& GetMixer() { return mixer; }
    auto GetSampleRate() const { return sample_rate; }
    auto GetTotalChannels() const { return total_channels; }
    auto GetFramesPerBlock() const { return frames_per_block; }

    auto GetSelectedIndex() const { return pa_selected_device; }
    // portaudio/examples/paex_sine_c++.cpp
    bool Open(PaDeviceIndex index);
private:
    bool Close();
    bool Start();
    bool Stop();
    bool Abort();
    int paCallbackMethod(
        const void* input_buffer, void* output_buffer,
        unsigned long frames_per_buffer,
        const PaStreamCallbackTimeInfo* time_info,
        PaStreamCallbackFlags status_flags);
    static int paCallback(
        const void* input_buffer, void* output_buffer,
        unsigned long frames_per_buffer,
        const PaStreamCallbackTimeInfo* time_info,
        PaStreamCallbackFlags status_flags,
        void* user_data);
    void paStreamFinishedCallbackMethod();
    static void paStreamFinishedCallback(void* user_data);
};