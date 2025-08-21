#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <complex>
#include <limits>
#include <vector>
#include <memory>
#include <string>
#include <optional>
#include "utility/span.h"
#include <fmt/core.h>
#include "./app_io_buffers.h"
#include "./app_wav_reader.h"

// T should be uint8_t, int8_t, uint16_t, int16_t, uint32_t, int32_t, uint64_t, int64_t
// bias for signed integers is 0, and floor(numeric_max/2) for unsigned integers
template <typename T>
struct QuantisedIQ {
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
    static inline QuantisedIQ<T> from_iq(float real, float imag) {
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
class QuantisedIQToFloatIQ: public InputBuffer<std::complex<float>>
{
private:
    std::shared_ptr<InputBuffer<QuantisedIQ<T>>> m_input = nullptr;
    std::vector<QuantisedIQ<T>> m_buffer;
public:
    QuantisedIQToFloatIQ(std::shared_ptr<InputBuffer<QuantisedIQ<T>>> input): m_input(input) {}
    ~QuantisedIQToFloatIQ() override = default;
    size_t read(tcb::span<std::complex<float>> dest) override {
        if (m_input == nullptr) return 0;
        m_buffer.resize(dest.size());
        const size_t length = m_input->read(m_buffer);
        // NOTE: normalise dequantisation to avoid extremely small/large values in postprocessing steps
        constexpr float scale = 1.0f/QuantisedIQ<T>::MAX_AMPLITUDE;
        for (size_t i = 0; i < length; i++) {
            const std::complex<float> v = m_buffer[i].to_c32();
            dest[i] = std::complex<float>(v.real()*scale, v.imag()*scale);
        }
        return length;
    }
};

template <typename T>
static std::shared_ptr<InputBuffer<std::complex<float>>> get_quantised_iq_file_reader(
    std::shared_ptr<InputBuffer<uint8_t>> src, const std::optional<bool> is_little_endian
) {
    const bool is_machine_little_endian = get_is_machine_little_endian();

    std::shared_ptr<InputBuffer<T>> component_stream = nullptr;
    component_stream = std::make_shared<ReinterpretCastInputBuffer<T, uint8_t>>(src);

    if (is_little_endian.has_value()) {
        const bool is_reverse_endian = is_machine_little_endian != is_little_endian.value();
        if (is_reverse_endian) component_stream = std::make_shared<ReverseEndian<T>>(component_stream);
    }
    auto raw_iq = std::make_shared<ReinterpretCastInputBuffer<QuantisedIQ<T>, T>>(component_stream);
    auto output_raw_iq = std::make_shared<QuantisedIQToFloatIQ<T>>(raw_iq);
    return output_raw_iq;
}

static const std::vector<std::string> iq_read_modes = {
    "wav",
    "raw_u8", "raw_s8",
    "raw_s16l", "raw_s16b", "raw_u16l", "raw_u16b",
    "raw_s32l", "raw_s32b", "raw_u32l", "raw_u32b",
    "raw_f32l", "raw_f32b", "raw_f64l", "raw_f64b",
};

static std::shared_ptr<InputBuffer<std::complex<float>>> get_iq_file_reader_from_mode_string(
    std::shared_ptr<InputFile<uint8_t>> file, const std::string& mode
) {
    if (mode == "wav") {
        auto wav_reader = std::make_shared<WavFileReader>(file);
        const auto& header = wav_reader->get_header();
        header.debug_print(stdout);
        if (header.total_channels != 2) {
            throw std::runtime_error(fmt::format(
                "WAV file should have 2 channels for IQ stream but got {} channels", header.total_channels
            ));
        }
        return std::make_shared<ReinterpretCastInputBuffer<std::complex<float>, float>>(wav_reader);
    }
    const bool is_machine_little_endian = get_is_machine_little_endian();
    if (mode == "raw_u8") return get_quantised_iq_file_reader<uint8_t>(file, std::nullopt);
    if (mode == "raw_s8") return get_quantised_iq_file_reader<int8_t>(file, std::nullopt);
    if (mode == "raw_s16l") return get_quantised_iq_file_reader<int16_t>(file, true);
    if (mode == "raw_s16b") return get_quantised_iq_file_reader<int16_t>(file, false);
    if (mode == "raw_u16l") return get_quantised_iq_file_reader<uint16_t>(file, true);
    if (mode == "raw_u16b") return get_quantised_iq_file_reader<uint16_t>(file, false);
    if (mode == "raw_s32l") return get_quantised_iq_file_reader<int32_t>(file, true);
    if (mode == "raw_s32b") return get_quantised_iq_file_reader<int32_t>(file, false);
    if (mode == "raw_u32l") return get_quantised_iq_file_reader<uint32_t>(file, true);
    if (mode == "raw_u32b") return get_quantised_iq_file_reader<uint32_t>(file, false);
    if (mode == "raw_f32l" || mode == "raw_f32b") {
        const bool is_little_endian = mode == "raw_f32l";
        const bool is_reverse_endian = is_machine_little_endian != is_little_endian;
        std::shared_ptr<InputBuffer<float>> f32_stream = nullptr;
        f32_stream = std::make_shared<ReinterpretCastInputBuffer<float, uint8_t>>(file);
        if (is_reverse_endian) f32_stream = std::make_shared<ReverseEndian<float>>(f32_stream);
        return std::make_shared<ReinterpretCastInputBuffer<std::complex<float>, float>>(f32_stream);
    }
    if (mode == "raw_f64l" || mode == "raw_f64b") {
        const bool is_little_endian = mode == "raw_f64l";
        const bool is_reverse_endian = is_machine_little_endian != is_little_endian;
        std::shared_ptr<InputBuffer<double>> f64_stream = nullptr;
        f64_stream = std::make_shared<ReinterpretCastInputBuffer<double, uint8_t>>(file);
        if (is_reverse_endian) f64_stream = std::make_shared<ReverseEndian<double>>(f64_stream);
        const auto f32_stream = std::make_shared<StaticCastInputBuffer<float, double>>(f64_stream);
        return std::make_shared<ReinterpretCastInputBuffer<std::complex<float>, float>>(f32_stream);
    }
    throw std::runtime_error(fmt::format("Unknown iq file format: '{}'", mode));
    return nullptr;
}
