#pragma once

#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <stdint.h>
#include "./frame.h"
#include "./ring_buffer.h"
#include "utility/span.h"

constexpr float DEFAULT_AUDIO_SAMPLE_RATE = 48000.0f;
constexpr float DEFAULT_AUDIO_SINK_DURATION = 0.1f;
constexpr float DEFAULT_AUDIO_SOURCE_DURATION = 0.3f;
constexpr size_t DEFAULT_AUDIO_SINK_SAMPLES = size_t(DEFAULT_AUDIO_SAMPLE_RATE * DEFAULT_AUDIO_SINK_DURATION);
constexpr size_t DEFAULT_AUDIO_SOURCE_SAMPLES = size_t(DEFAULT_AUDIO_SAMPLE_RATE * DEFAULT_AUDIO_SOURCE_DURATION);

class AudioPipelineSink
{
public:
    using Callback = std::function<void(tcb::span<Frame<float>>, float)>;
    virtual ~AudioPipelineSink() {}
    virtual void set_callback(Callback callback) = 0;
    virtual std::string_view get_name() const = 0;
};

class AudioPipelineSource 
{
private:
    const float m_sampling_rate;
    float m_gain = 1.0f;

    std::vector<Frame<float>> m_resampling_buffer;
    RingBuffer<Frame<float>> m_ring_buffer;
    std::vector<Frame<float>> m_read_buffer;

    std::mutex m_mutex_ring_buffer;
    std::condition_variable m_cv_ring_buffer;
public:
    explicit AudioPipelineSource(float sampling_rate=DEFAULT_AUDIO_SAMPLE_RATE, size_t buffer_length=DEFAULT_AUDIO_SOURCE_SAMPLES);
    void read_from_source(tcb::span<const Frame<int16_t>> src, float src_sampling_rate, bool is_blocking); 
    bool write_to_dest(tcb::span<Frame<float>> dest);
    float get_sampling_rate() const { return m_sampling_rate; }
};

class AudioPipeline
{
private:
    float m_global_gain = 1.0f;
    std::vector<std::shared_ptr<AudioPipelineSource>> m_sources;
    std::unique_ptr<AudioPipelineSink> m_sink = nullptr;
    std::vector<Frame<float>> m_read_buffer;
public:
    AudioPipeline() {}
    void set_sink(std::unique_ptr<AudioPipelineSink>&& sink);
    AudioPipelineSink* get_sink() { return m_sink.get(); }
    void add_source(std::shared_ptr<AudioPipelineSource>& source) { m_sources.push_back(source); }
    void clear_sources() { m_sources.clear(); }
    float& get_global_gain() { return m_global_gain; }
private:
    void mix_sources_to_sink(tcb::span<Frame<float>> dest, float dest_sampling_rate);
};
