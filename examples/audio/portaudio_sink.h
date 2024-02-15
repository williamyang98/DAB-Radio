#pragma once

#include <portaudio.h>
#include <string>
#include <vector>
#include "utility/span.h"
#include "./frame.h"
#include "./audio_pipeline.h"

// Create this once before all port audio code
class PortAudioGlobalHandler {
private:
    PaError m_result;
public:
    PortAudioGlobalHandler(): m_result(Pa_Initialize()) {}
    ~PortAudioGlobalHandler() { if (m_result == paNoError) Pa_Terminate(); }
    PaError get_result() const { return m_result; }
};

struct PortAudioDevice {
    PaDeviceIndex device_index;
    PaHostApiIndex host_api_index;
    std::string label;
};

std::vector<PortAudioDevice> get_portaudio_devices();
PaDeviceIndex get_default_portaudio_device_index();

enum class PortAudioSinkCreateError {
    FAILED_INIT,
    FAILED_DEVICE_NONE,
    FAILED_DEVICE_INFO,
    FAILED_DEVICE_OPEN,
    FAILED_DEVICE_START,
    SUCCESS,
};

class PortAudioSink;
struct PortAudioSinkCreateResult {
    std::unique_ptr<PortAudioSink> sink;
    PortAudioSinkCreateError error;
};

class PortAudioSink: public AudioPipelineSink
{
private:
    PaStream* m_portaudio_stream = nullptr;
    const float m_sample_rate;
    const size_t m_frames_per_buffer;
    // Portaudio might be allocating this temporarily inside PaDeviceInfo.name so just copy it
    const std::string m_device_name;
    AudioPipelineSink::Callback m_callback;
public:
    explicit PortAudioSink(float sample_rate, size_t frames_per_buffer, const std::string&& device_name)
    : m_sample_rate(sample_rate), m_frames_per_buffer(frames_per_buffer), m_device_name(device_name) {}
    ~PortAudioSink() override;
    PortAudioSink(const PortAudioSink&) = delete;
    PortAudioSink(PortAudioSink&&) = delete;
    PortAudioSink& operator=(const PortAudioSink&) = delete;
    PortAudioSink& operator=(PortAudioSink&&) = delete;
    void set_callback(AudioPipelineSink::Callback callback) override { m_callback = callback; }
    std::string_view get_name() const override { return m_device_name; }
    size_t get_frames_per_buffer() const { return m_frames_per_buffer; }
    static PortAudioSinkCreateResult create_from_index(
        PaDeviceIndex index, 
        float sample_rate=DEFAULT_AUDIO_SAMPLE_RATE, 
        size_t frames_per_buffer=DEFAULT_AUDIO_SINK_SAMPLES
    );
    static int _portaudio_callback(
        const void* input_buffer, void* output_buffer, 
        unsigned long frames_per_buffer, 
        const PaStreamCallbackTimeInfo* time_info,
        PaStreamCallbackFlags flags,
        void* user_data
    );
};

