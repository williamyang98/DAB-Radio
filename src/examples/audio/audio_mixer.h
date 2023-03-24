#pragma once

#include "./frame.h"
#include "./ring_buffer.h"
#include "utility/span.h"
#include <stdint.h>
#include <vector>
#include <memory>
#include <mutex>

class AudioMixer 
{
private:
    float output_gain;
    std::vector<std::shared_ptr<RingBuffer<Frame<int16_t>>>> input_buffers;
    std::vector<Frame<float>> mixer_buf;
    std::vector<RingBuffer<Frame<int16_t>>::scoped_buffer_t> pending_buffers;
    const int block_size;
    std::mutex mutex_buffers;
public:
    explicit AudioMixer(const int _block_size=2);
    std::shared_ptr<RingBuffer<Frame<int16_t>>> CreateManagedBuffer(const int nb_blocks);
    tcb::span<Frame<float>> UpdateMixer();
    float& GetOutputGain() { return output_gain; };
};