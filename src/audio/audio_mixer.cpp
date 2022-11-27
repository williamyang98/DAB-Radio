#include "audio_mixer.h"
#include <algorithm>
#include <cstring>

AudioMixer::AudioMixer(const int _block_size)
: block_size(_block_size)
{
    mixer_buf.resize(block_size);
    output_buf.resize(block_size);
}

std::shared_ptr<RingBuffer<Frame<int16_t>>> AudioMixer::CreateManagedBuffer(const int nb_blocks) 
{
    auto buf = std::make_shared<RingBuffer<Frame<int16_t>>>(block_size, nb_blocks);
    auto lock = std::scoped_lock(mutex_buffers);
    input_buffers.push_back(buf);
    return buf;
}

tcb::span<Frame<int16_t>> AudioMixer::UpdateMixer() {

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
        std::memset(output_buf.data(), 0, output_buf.size() * sizeof(Frame<int16_t>));
        return output_buf;
    }

    if (total_sources == 1) {
        auto rd_buf = pending_buffers[0].buf;
        std::copy_n(rd_buf.begin(), block_size, output_buf.begin());
        pending_buffers.clear();
        return output_buf;
    }

    // Only perform mixing if we have more than one source playing
    std::memset(mixer_buf.data(), 0, mixer_buf.size()*sizeof(Frame<int32_t>));
    const int N = (int)mixer_buf.size();

    for (auto& pending_buffer: pending_buffers) {
        auto rd_buf = pending_buffer.buf;
        for (int i = 0; i < N; i++) {
            mixer_buf[i] += (Frame<int32_t>)rd_buf[i];
        }
    }

    for (int i = 0; i < N; i++) {
        output_buf[i] = mixer_buf[i] / total_sources;
    }

    pending_buffers.clear();
    return output_buf;
}