#pragma once

#include "frame.h"
#include "ring_buffer.h"
#include <stdint.h>
#include <memory>
#include <vector>

class Resampled_PCM_Player
{
private:
    int input_sample_rate;
    const int output_sample_rate;
    std::shared_ptr<RingBuffer<Frame<int16_t>>> buffer;
    std::vector<Frame<int16_t>> resampling_buffer;
public:
    Resampled_PCM_Player(std::shared_ptr<RingBuffer<Frame<int16_t>>> _buffer, int _output_sample_rate);
    virtual void ConsumeBuffer(tcb::span<const Frame<int16_t>> buf);
    virtual bool SetInputSampleRate(const int _input_sample_rate);
};