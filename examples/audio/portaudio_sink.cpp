#include "./portaudio_sink.h"
#include <stdio.h>
#include <stdlib.h>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <portaudio.h>
#include "utility/span.h"
#include "./frame.h"

#if _WIN32
constexpr PaHostApiTypeId PORTAUDIO_TARGET_HOST_API_ID = PaHostApiTypeId::paDirectSound;
#endif

std::vector<PortAudioDevice> get_portaudio_devices() {
    std::vector<PortAudioDevice> devices;
    const int total_devices = Pa_GetDeviceCount();
    if (total_devices < 0) {
        fprintf(stderr, "ERROR: [get_portaudio_devices] failed to get device count %d\n", total_devices);
        return devices;
    }
    for (int i = 0; i < total_devices; i++) {
        const PaDeviceInfo* device_info = Pa_GetDeviceInfo(i);
        if (device_info == nullptr) {
            fprintf(stderr, "WARN: [get_portaudio_devices] device=%d, failed to get device info\n", i);
            continue;
        }
        if (device_info->maxOutputChannels <= 0) {
            // fprintf(stderr, "WARN: [get_portaudio_devices] device=%d, device has invalid number of channels (%d)\n", i, device_info->maxOutputChannels);
            continue;
        }
        #if _WIN32
        const auto target_host_api_index = Pa_HostApiTypeIdToHostApiIndex(PORTAUDIO_TARGET_HOST_API_ID);
        if (device_info->hostApi != target_host_api_index) {
            continue;
        }
        #endif
        PortAudioDevice device;
        device.label = std::string(device_info->name);
        device.device_index = i;
        device.host_api_index = device_info->hostApi;
        devices.push_back(device);
    }
    return devices;
}

PaDeviceIndex get_default_portaudio_device_index() {
#if _WIN32
    const auto target_host_api_index = Pa_HostApiTypeIdToHostApiIndex(PORTAUDIO_TARGET_HOST_API_ID);
    const auto target_device_index = Pa_GetHostApiInfo(target_host_api_index)->defaultOutputDevice;
#else
    const auto target_device_index = Pa_GetDefaultOutputDevice();
#endif
    return target_device_index;
}

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

