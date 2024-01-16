#include "./audio_pipeline.h"
#include <assert.h>
#include <cmath>
#include <cstring>
#include <limits>

template <typename T, typename U, typename F>
static void audio_map_with_callback(tcb::span<const Frame<T>> src, tcb::span<Frame<U>> dest, F&& func) {
    assert(src.size() == dest.size());
    const size_t N = src.size();
    for (size_t i = 0; i < N; i++) {
        func(dest[i], src[i]);
    }
}

template <typename T, typename U, typename F>
static void audio_resample_with_callback(tcb::span<const Frame<T>> src, tcb::span<Frame<U>> dest, F&& func) {
    const size_t N_dest = dest.size();
    const size_t N_src = src.size();
    const float ratio = float(N_src-1) / float(N_dest-1);
    for (size_t dest_i = 0; dest_i < N_dest; dest_i++) {
        const float src_i = float(dest_i) * ratio;
        const size_t src_i0 = size_t(src_i);
        size_t src_i1 = src_i0 + 1;
        if (src_i1 >= N_src) {
            src_i1 = N_src-1;
        }
        const float k = src_i - float(src_i0);
        const auto v0 = static_cast<const Frame<float>>(src[src_i0]);
        const auto v1 = static_cast<const Frame<float>>(src[src_i1]);
        const auto v2 = v0*(1.0f-k) + v1*k;
        func(dest[dest_i], v2);
    }
}

template <typename T, typename F>
static void audio_resample_same_type_with_callback(tcb::span<const Frame<T>> src, tcb::span<Frame<T>> dest, F&& func) {
    if (src.size() == dest.size()) {
        audio_map_with_callback<T,T>(src, dest, std::move(func));
    } else {
        audio_resample_with_callback<T,T>(src, dest, std::move(func));
    }
}

template <typename T>
static void audio_clamp_inplace(tcb::span<Frame<T>> buf, const T v_min, const T v_max) {
    const size_t N = buf.size();
    for (size_t i = 0; i < N; i++) {
        auto& v = buf[i];
        for (int j = 0; j < Frame<T>::TOTAL_AUDIO_CHANNELS; j++) {
            auto& x = v.channels[j];
            x = (x > v_min) ? x : v_min;
            x = (x > v_max) ? v_max : x;
        }
    }
}

AudioPipelineSource::AudioPipelineSource(float sampling_rate, size_t buffer_length)
: m_sampling_rate(sampling_rate), m_ring_buffer(buffer_length)
{}

void AudioPipelineSource::read_from_source(tcb::span<const Frame<int16_t>> src, float src_sampling_rate, bool is_blocking) {
    const float gain = m_gain / float(std::numeric_limits<int16_t>::max());
    const size_t resample_length = size_t(float(src.size()) * m_sampling_rate / src_sampling_rate);
    m_resampling_buffer.resize(resample_length);

    if (resample_length == src.size()) {
        audio_map_with_callback<int16_t,float>(
            src, m_resampling_buffer, 
            [gain](Frame<float>& dest, const Frame<int16_t>& src) {
                dest = static_cast<Frame<float>>(src) * gain;
            }
        );
    } else {
        audio_resample_with_callback<int16_t,float>(
            src, m_resampling_buffer, 
            [gain](Frame<float>& dest, const Frame<float>& src) {
                dest = src * gain;
            }
        );
    }

    auto lock = std::unique_lock(m_mutex_ring_buffer);
    if (!is_blocking) {
        m_ring_buffer.write_from_src_with_overwrite(m_resampling_buffer);
        return;
    }

    auto read_buffer = tcb::span<const Frame<float>>(m_resampling_buffer);
    while (true) {
        const size_t total_read = m_ring_buffer.write_from_src_until_full(read_buffer);
        read_buffer = read_buffer.subspan(total_read);
        if (read_buffer.empty()) break;
        m_cv_ring_buffer.wait(lock, [this]{ 
            return !m_ring_buffer.is_full();
        });
    }
}

bool AudioPipelineSource::write_to_dest(tcb::span<Frame<float>> dest) {
    auto lock = std::unique_lock(m_mutex_ring_buffer);
    if (m_ring_buffer.get_total_used() < dest.size()) {
        return false;
    }
    m_ring_buffer.read_to_dest(dest);
    lock.unlock();
    m_cv_ring_buffer.notify_one();
    return true;
}

void AudioPipeline::set_sink(std::unique_ptr<AudioPipelineSink>&& sink) {
    m_sink = std::move(sink);
    if (m_sink == nullptr) return;
    m_sink->set_callback([this](tcb::span<Frame<float>> dest, float dest_sampling_rate) {
        mix_sources_to_sink(dest, dest_sampling_rate); 
    });
}

void AudioPipeline::mix_sources_to_sink(tcb::span<Frame<float>> dest, float dest_sampling_rate) {
    const size_t N_dest = dest.size();
    std::memset(dest.data(), 0, sizeof(Frame<float>) * N_dest);

    size_t total_sources_mixed = 0;
    for (auto& source: m_sources) {
        const float src_sampling_rate = source->get_sampling_rate();
        const size_t N_src = size_t(float(N_dest) * src_sampling_rate / dest_sampling_rate);
        m_read_buffer.resize(N_src);

        if (!source->write_to_dest(m_read_buffer)) {
            continue;
        }

        audio_resample_same_type_with_callback<float>(
            m_read_buffer, dest,
            [](Frame<float>& dest, const Frame<float>& src) { 
                dest += src;
            }
        );
        total_sources_mixed++;
    }

    const float gain = m_global_gain / std::log10(float(total_sources_mixed * 10.0f));
    for (auto& v: dest) {
        v = v * gain;
    }

    audio_clamp_inplace(dest, -1.0f, 1.0f);
}
