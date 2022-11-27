#pragma once

#include "frame.h"
#include "ring_buffer.h"
#include "utility/span.h"
#include <stdint.h>
#include <vector>
#include <memory>
#include <mutex>

class AudioMixer 
{
private:
    std::vector<std::shared_ptr<RingBuffer<Frame<int16_t>>>> input_buffers;
    std::vector<Frame<int32_t>> mixer_buf;
    std::vector<Frame<int16_t>> output_buf;
    std::vector<RingBuffer<Frame<int16_t>>::scoped_buffer_t> pending_buffers;
    const int block_size;
    std::mutex mutex_buffers;
public:
    AudioMixer(const int _block_size=2);
    std::shared_ptr<RingBuffer<Frame<int16_t>>> CreateManagedBuffer(const int nb_blocks);
    tcb::span<Frame<int16_t>> UpdateMixer();
};