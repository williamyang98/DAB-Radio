#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <complex>
#include <limits>
#include <vector>
#include <memory>
#include "utility/span.h"
#include "./app_io_buffers.h"

template <typename T>
static void reverse_endian_inplace(tcb::span<T> dest) {
    constexpr size_t stride = sizeof(T);
    auto dest_bytes = tcb::span<uint8_t>(
        reinterpret_cast<uint8_t*>(dest.data()),
        dest.size()*stride
    );
    constexpr size_t total_flip = stride/2;
    for (size_t i = 0; i < dest_bytes.size(); i+=stride) {
        for (size_t j_src = 0; j_src < total_flip; j_src++) {
            const size_t j_dst = stride-j_src-1;
            const uint8_t tmp_src = dest_bytes[i+j_src];
            dest_bytes[i+j_src] = dest_bytes[i+j_dst];
            dest_bytes[i+j_dst] = tmp_src;
        }
    }
}

template <typename T>
class ReverseEndian: public InputBuffer<T>
{
private:
    std::shared_ptr<InputBuffer<T>> m_input = nullptr;
public:
    ReverseEndian() {}
    ~ReverseEndian() override = default;
    void set_input_stream(std::shared_ptr<InputBuffer<T>> input) { 
        m_input = input;
    }
    size_t read(tcb::span<T> dest) override {
        if (m_input == nullptr) return 0;
        const size_t length = m_input->read(dest);
        reverse_endian_inplace(dest.first(length));
        return length;
    }
};

// T should be uint8_t, int8_t, uint16_t, int16_t, uint32_t, int32_t, uint64_t, int64_t
// bias for signed integers is 0, and floor(numeric_max/2) for unsigned integers
template <typename T>
struct RawIQ {
    T I;
    T Q;
    constexpr static float BIAS =
        std::numeric_limits<T>::is_signed ?
        0.0f :
        static_cast<float>(std::numeric_limits<T>::max()/T(2)) + 0.5f;
    constexpr static float MAX_AMPLITUDE =
        std::numeric_limits<T>::is_signed ?
        static_cast<float>(std::numeric_limits<T>::max()) :
        static_cast<float>(std::numeric_limits<T>::max()/T(2)) + 0.5f;
    inline std::complex<float> to_c32() const {
        if constexpr (BIAS == 0.0f) {
            return std::complex<float>(
                static_cast<float>(I),
                static_cast<float>(Q)
            );
        } else {
            return std::complex<float>(
                static_cast<float>(I) - BIAS,
                static_cast<float>(Q) - BIAS
            );
        }
    }
    template <typename U>
    static U clamp(U x, const U min, const U max) {
        U y = x;
        y = (y > min) ? y : min;
        y = (y > max) ? max : y;
        return y;
    }
    static inline RawIQ<T> from_iq(float real, float imag) {
        if constexpr (BIAS != 0.0f) {
            real += BIAS;
            imag += BIAS;
        }
        constexpr float v_min = static_cast<float>(std::numeric_limits<T>::min());
        constexpr float v_max = static_cast<float>(std::numeric_limits<T>::max());
        real = clamp(real, v_min, v_max);
        imag = clamp(imag, v_min, v_max);
        const T I = static_cast<T>(real);
        const T Q = static_cast<T>(imag);
        return { I, Q };
    }
};

template <typename T>
class RawIQToFloat: public InputBuffer<std::complex<float>>
{
private:
    std::shared_ptr<InputBuffer<RawIQ<T>>> m_input = nullptr;
    std::vector<RawIQ<T>> m_buffer;
public:
    RawIQToFloat() {}
    ~RawIQToFloat() override = default;
    void set_input_stream(std::shared_ptr<InputBuffer<RawIQ<T>>> input) { 
        m_input = input;
    }
    size_t read(tcb::span<std::complex<float>> dest) override {
        if (m_input == nullptr) return 0;
        m_buffer.resize(dest.size());
        const size_t length = m_input->read(m_buffer);
        // NOTE: normalise dequantisation to avoid extremely small/large values in postprocessing steps
        constexpr float scale = 1.0f/RawIQ<T>::MAX_AMPLITUDE;
        for (size_t i = 0; i < length; i++) {
            const std::complex<float> v = m_buffer[i].to_c32();
            dest[i] = std::complex<float>(v.real()*scale, v.imag()*scale);
        }
        return length;
    }
};

static bool get_is_machine_little_endian() {
    volatile union {
        uint32_t value;
        uint8_t data[4];
    } e;
    e.value = 0x00000001;
    return e.data[0] == 0x01;
}

template <typename T>
static std::shared_ptr<InputBuffer<std::complex<float>>> get_raw_iq_file_reader(
    std::shared_ptr<InputBuffer<uint8_t>> src, const bool is_little_endian
) {
    const bool is_machine_little_endian = get_is_machine_little_endian();
    const bool is_reverse_endian = is_machine_little_endian != is_little_endian;

    std::shared_ptr<InputBuffer<T>> component_stream = nullptr;
    {
        const auto bytes_to_component = std::make_shared<ConvertInputBuffer<T, uint8_t>>();
        bytes_to_component->set_input_stream(src);
        component_stream = bytes_to_component;
    }
    if (is_reverse_endian) {
        const auto reversed_component_stream = std::make_shared<ReverseEndian<T>>();
        reversed_component_stream->set_input_stream(component_stream);
        component_stream = reversed_component_stream;
    }

    auto raw_iq = std::make_shared<ConvertInputBuffer<RawIQ<T>, T>>();
    raw_iq->set_input_stream(component_stream);
    auto output_raw_iq = std::make_shared<RawIQToFloat<T>>();
    output_raw_iq->set_input_stream(raw_iq);
    return output_raw_iq;
}
