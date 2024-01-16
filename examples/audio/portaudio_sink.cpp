#include "./portaudio_sink.h"

PortAudioSinkCreateResult PortAudioSink::create_from_index(PaDeviceIndex index, float sample_rate, size_t frames_per_buffer) {
    if (index == paNoDevice) {
        return PortAudioSinkCreateResult { nullptr, PortAudioSinkCreateError::FAILED_DEVICE_NONE };
    }

    const auto* info = Pa_GetDeviceInfo(index);
    if (info == nullptr) {
        return PortAudioSinkCreateResult { nullptr, PortAudioSinkCreateError::FAILED_DEVICE_INFO };
    }

    PaStreamParameters params;
    params.device = index;
    params.channelCount = 2;
    params.sampleFormat = paFloat32;
    params.suggestedLatency = info->defaultLowOutputLatency;
    params.hostApiSpecificStreamInfo = nullptr;

    auto sink = std::make_unique<PortAudioSink>(sample_rate, frames_per_buffer, std::string(info->name));

    const PaError err_open = Pa_OpenStream(
        &(sink->m_portaudio_stream), nullptr, 
        &params, double(sample_rate), (unsigned long)(frames_per_buffer), paClipOff, 
        &PortAudioSink::_portaudio_callback, reinterpret_cast<void*>(sink.get())
    );

    if (err_open != paNoError) {
        return PortAudioSinkCreateResult { nullptr, PortAudioSinkCreateError::FAILED_DEVICE_OPEN };
    }

    const PaError err_start = Pa_StartStream(sink->m_portaudio_stream);
    if (err_start != paNoError) {
        return PortAudioSinkCreateResult { nullptr, PortAudioSinkCreateError::FAILED_DEVICE_START };
    }

    return PortAudioSinkCreateResult { std::move(sink), PortAudioSinkCreateError::SUCCESS };
}

PortAudioSink::~PortAudioSink() {
    if (m_portaudio_stream == nullptr) return;
    Pa_AbortStream(m_portaudio_stream);
    m_portaudio_stream = nullptr;
}

int PortAudioSink::_portaudio_callback(
    const void* input_buffer, void* output_buffer, 
    unsigned long frames_per_buffer, 
    const PaStreamCallbackTimeInfo* time_info,
    PaStreamCallbackFlags flags,
    void* user_data
) {
    if (user_data == nullptr) {
        return paContinue;
    }
    auto& sink = *reinterpret_cast<PortAudioSink*>(user_data);
    if (!sink.m_callback) {
        return paContinue;
    }

    const auto write_buffer = tcb::span<Frame<float>>(
        reinterpret_cast<Frame<float>*>(output_buffer),
        size_t(frames_per_buffer)
    );

    sink.m_callback(write_buffer, sink.m_sample_rate);
    return paContinue;
}

