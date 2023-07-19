#include "./audio_mixer.h"
#include <algorithm>
#include <cstring>
#include <cmath>

AudioMixer::AudioMixer(const int _block_size)
: block_size(_block_size)
{
    output_gain = 1.0f;
    mixer_buf.resize(block_size);
}

std::shared_ptr<RingBuffer<Frame<int16_t>>> AudioMixer::CreateManagedBuffer(const int nb_blocks) 
{
    auto buf = std::make_shared<RingBuffer<Frame<int16_t>>>(block_size, nb_blocks);
    auto lock = std::scoped_lock(mutex_buffers);
    input_buffers.push_back(buf);
    return buf;
}

template <typename T>
inline Frame<T> clamp(const Frame<T> X, const T min, const T max) {
    Frame<T> Y;
    for (int i = 0; i < TOTAL_AUDIO_CHANNELS; i++) {
        auto& x = X.channels[i];
        auto& y = Y.channels[i];
        y = (x > min) ? x : min;
        y = (y > max) ? max : y;
    }
    return Y;
}

tcb::span<Frame<float>> AudioMixer::UpdateMixer() {

    auto lock = std::scoped_lock(mutex_buffers);
    for (auto& input_buffer: input_buffers) {
        // Early exit check so we don't incur overhead acquire mutex for each buffer
        if (input_buffer->GetTotalBlocks() == 0) {
            continue;
        }

        auto scoped_buf = input_buffer->PopBlock();
        if (scoped_buf.buf.empty()) {
            continue;
        }

        pending_buffers.push_back(std::move(scoped_buf));
    }

    const int total_sources = (int)pending_buffers.size();

    if (total_sources == 0) {
        std::memset(mixer_buf.data(), 0, mixer_buf.size() * sizeof(Frame<float>));
        return mixer_buf;
    }

    std::memset(mixer_buf.data(), 0, mixer_buf.size()*sizeof(Frame<float>));
    const int N = (int)mixer_buf.size();

    float scale = output_gain; 
    scale /= (float)(INT16_MAX);
    // TODO: I'm not an audio engineer I have no idea how to mix audio properly
    //       Replace with existing audio DSP libraries
    scale /= std::log10((float)(total_sources * 10.0f));

    for (auto& pending_buffer: pending_buffers) {
        auto rd_buf = pending_buffer.buf;
        for (int i = 0; i < N; i++) {
            mixer_buf[i] += ((Frame<float>)rd_buf[i]) * scale;
        }
    }

    // Clamp audio to prevent clipping
    const float max_amplitude = 1.0f;
    for (int i = 0; i < N; i++) {
        mixer_buf[i] = clamp(mixer_buf[i], -max_amplitude, max_amplitude);
    }

    pending_buffers.clear();
    return mixer_buf;
}