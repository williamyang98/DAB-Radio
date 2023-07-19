#include "./portaudio_output.h"
#include <algorithm>
#include <stdio.h>

#define LOG_MESSAGE(fmt, ...) fprintf(stderr, "[portaudio] " fmt "\n", ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) fprintf(stderr, "ERROR: [portaudio] " fmt "\n", ##__VA_ARGS__)

PortAudio_Output::PortAudio_Output(const int _sample_rate) 
: sample_rate(_sample_rate), total_channels(TOTAL_AUDIO_CHANNELS),
  frames_per_block(_sample_rate/10),
  mixer(frames_per_block)
{
    pa_stream = NULL;
    pa_selected_device = paNoDevice;
    is_running = false;
}

PortAudio_Output::~PortAudio_Output() {
    Abort();
}

// portaudio/examples/paex_sine_c++.cpp
bool PortAudio_Output::Open(PaDeviceIndex index) {
    auto lock = std::scoped_lock(mutex_pa_stream);

    if (pa_stream != NULL) {
        Stop();
        Close();
    }

    PaStreamParameters output_params;
    output_params.device = index;
    if (output_params.device == paNoDevice) {
        return false;
    }

    const PaDeviceInfo* pInfo = Pa_GetDeviceInfo(index);
    if (pInfo == NULL) {
        return false;
    }

    LOG_MESSAGE("Output device name: '%s'", pInfo->name);
    output_params.channelCount = total_channels;       
    output_params.sampleFormat = paFloat32;
    output_params.suggestedLatency = Pa_GetDeviceInfo(output_params.device)->defaultLowOutputLatency;
    output_params.hostApiSpecificStreamInfo = NULL;

    PaError err = Pa_OpenStream(
        &pa_stream,
        NULL, &output_params,
        (double)sample_rate, (unsigned long)frames_per_block,
        paClipOff, &PortAudio_Output::paCallback, this);

    if (err != paNoError) {
        LOG_ERROR("Failed to open stream %d", err);
        return false;
    }

    err = Pa_SetStreamFinishedCallback(pa_stream, &PortAudio_Output::paStreamFinishedCallback);
    if (err != paNoError) {
        PaError err = Pa_CloseStream(pa_stream);
        pa_stream = NULL;
        LOG_ERROR("Failed to set stream finished callback %d", err);
        return false;
    }

    pa_selected_device = index;
    LOG_MESSAGE("Selected device %d", index);
    Start();
    return true;
}

int PortAudio_Output::paCallbackMethod(
    const void* input_buffer, void* output_buffer,
    unsigned long frames_per_buffer,
    const PaStreamCallbackTimeInfo* time_info,
    PaStreamCallbackFlags status_flags)
{
    if (!is_running) {
        return paAbort;
    }

    auto pa_lock = std::scoped_lock(mutex_pa_stream);
    auto rd_buf = mixer.UpdateMixer();

    if (frames_per_block != rd_buf.size()) {
        LOG_ERROR("Mixer buffer doesn't match expected block size %d!=%d", frames_per_block, (int)rd_buf.size());
        return paAbort;
    }

    tcb::span<Frame<float>> wr_buf = { 
        reinterpret_cast<Frame<float>*>(output_buffer), 
        (size_t)frames_per_block
    };
    std::copy_n(rd_buf.begin(), frames_per_block, wr_buf.begin());
    return paContinue;
}

void PortAudio_Output::paStreamFinishedCallbackMethod() {
    // auto pa_lock = std::scoped_lock(mutex_pa_stream);
    LOG_MESSAGE("Stream finished callback");
    // is_running = false;
}

int PortAudio_Output::paCallback(
    const void* input_buffer, void* output_buffer,
    unsigned long frames_per_buffer,
    const PaStreamCallbackTimeInfo* time_info,
    PaStreamCallbackFlags status_flags,
    void* user_data) 
{
    auto* instance = reinterpret_cast<PortAudio_Output*>(user_data);
    return instance->paCallbackMethod(
        input_buffer, output_buffer,
        frames_per_buffer,
        time_info,
        status_flags);
}

void PortAudio_Output::paStreamFinishedCallback(void* user_data) {
    auto* instance = reinterpret_cast<PortAudio_Output*>(user_data);
    instance->paStreamFinishedCallbackMethod();
}

bool PortAudio_Output::Close() {
    if (pa_stream == NULL) {
        return false;
    }
    if (is_running) {
        Stop();
    }
    PaError err = Pa_CloseStream(pa_stream);
    pa_stream = NULL;
    pa_selected_device = paNoDevice;
    is_running = false;
    const bool rv = (err == paNoError);
    LOG_MESSAGE("action=close status=%d", rv);
    return rv;
}

bool PortAudio_Output::Start() {
    if (is_running) {
        return true;
    }
    if (pa_stream == NULL) {
        return false;
    }
    PaError err = Pa_StartStream(pa_stream);
    const bool rv = (err == paNoError);
    LOG_MESSAGE("action=start status=%d", rv);
    is_running = true;
    return rv;
}

bool PortAudio_Output::Stop() {
    if (!is_running) {
        return false;
    }
    if (pa_stream == NULL) {
        return false;
    }
    PaError err = Pa_StopStream(pa_stream);
    const bool rv = (err == paNoError);
    LOG_MESSAGE("action=stop status=%d", rv);
    is_running = false;
    return rv;
}

bool PortAudio_Output::Abort() {
    if (!is_running) {
        return false;
    }
    if (pa_stream == NULL) {
        return false;
    }
    PaError err = Pa_AbortStream(pa_stream);
    const bool rv = (err == paNoError);
    LOG_MESSAGE("action=abort status=%d", rv);
    is_running = false;
    return rv;
}
