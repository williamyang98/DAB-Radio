#pragma once

#include <stdint.h>
#include <stdlib.h>
#include <stdexcept>
#include <string>
#include <vector>
#include <limits>
#include <fmt/core.h>
#include <optional>
#include <memory>
#include <functional>
#include "utility/span.h"
#include "./app_io_buffers.h"

enum class WavAudioFormat: uint16_t {
    PCM = 0x0001,
    // https://en.wikipedia.org/wiki/IEEE_754#Basic_and_interchange_formats
    IEEE754_FLOAT = 0x0003,
    // https://en.wikipedia.org/wiki/G.711#Types
    G711_A_LAW = 0x0006,
    G711_MU_LAW = 0x0007,
    EXTENSIBLE = 0xFFFE,
};

struct WavHeader {
    struct UnhandledChunk {
        std::string id;
        uint32_t size_bytes;
    };
    WavAudioFormat format;
    uint16_t total_channels;
    uint32_t samples_per_second;
    uint32_t average_bytes_per_second;
    uint16_t data_block_align_bytes;
    uint16_t bits_per_sample;
    std::optional<uint16_t> valid_bits_per_sample = std::nullopt;
    std::optional<uint32_t> channel_mask = std::nullopt;
    std::optional<std::string> sub_format = std::nullopt;
    std::optional<uint32_t> total_samples_per_channel = std::nullopt;
    std::vector<UnhandledChunk> unhandled_chunks = {};
    uint32_t data_chunk_size;
    size_t data_chunk_offset;
    void debug_print(FILE* fp_out) const {
        const char* format_string = nullptr;
        switch (format) {
            case WavAudioFormat::PCM: format_string = "pcm"; break;
            case WavAudioFormat::IEEE754_FLOAT: format_string = "ieee754_float"; break;
            case WavAudioFormat::G711_A_LAW: format_string = "g711_a_law"; break;
            case WavAudioFormat::G711_MU_LAW: format_string = "g711_mu_law"; break;
            case WavAudioFormat::EXTENSIBLE: format_string = "extensible"; break;
            default: format_string = "(unknown)";
        }
        fprintf(fp_out, "format = %s\n", format_string);
        fprintf(fp_out, "total_channels = %u\n", total_channels);
        fprintf(fp_out, "samples_per_second = %u\n", samples_per_second);
        fprintf(fp_out, "average_bytes_per_second = %u\n", average_bytes_per_second);
        fprintf(fp_out, "data_block_align_bytes = %u\n", data_block_align_bytes);
        fprintf(fp_out, "bits_per_sample = %u\n", bits_per_sample);
        if (valid_bits_per_sample.has_value()) {
            fprintf(fp_out, "valid_bits_per_sample = %u\n", valid_bits_per_sample.value());
        }
        if (channel_mask.has_value()) {
            fprintf(fp_out, "channel_mask = %u\n", channel_mask.value());
        }
        if (sub_format.has_value()) {
            fprintf(fp_out, "sub_format = [");
            const auto buf = tcb::span<const uint8_t>(
                reinterpret_cast<const uint8_t*>(sub_format.value().data()),
                sub_format.value().size()
            );
            const size_t N = buf.size();
            for (size_t i = 0; i < N; i++) {
                fprintf(fp_out, "0x%02X", buf[i]);
                if (i < N-1) fprintf(fp_out, ",");
            }
            fprintf(fp_out, "]\n");
        }
        if (total_samples_per_channel.has_value()) {
            fprintf(fp_out, "total_samples_per_channel = %u\n", total_samples_per_channel.value());
        }
        fprintf(fp_out, "unhandled_chunks = [");
        if (unhandled_chunks.size() > 0) {
            fprintf(fp_out, "\n");
        }
        for (const auto& chunk: unhandled_chunks) {
            fprintf(fp_out, "  ( id=%.*s, size=%u ),\n", int(chunk.id.length()), chunk.id.data(), chunk.size_bytes);
        }
        fprintf(fp_out, "]\n");
        fprintf(fp_out, "data_chunk_size = %u\n", data_chunk_size);
        fprintf(fp_out, "data_chunk_offset = %zu\n", data_chunk_offset);
    }
};

template <typename T>
static T read_wav_header_value(tcb::span<const uint8_t> &buf) {
    T x = T(0);
    constexpr size_t N = sizeof(T);
    const auto data = buf.first(N);
    buf = buf.subspan(N);
    for (size_t i = 0; i < N; i++) {
        x |= data[i] << i*8;
    }
    return x;
}

static WavHeader wav_read_header(FILE* fp_in) {
    // https://www.mmsp.ece.mcgill.ca/Documents/AudioFormats/WAVE/WAVE.html
    auto read_buffer = std::vector<uint8_t>();
    size_t read_offset = 0;
    WavHeader header;

    const auto try_read_bytes = [&read_buffer, &read_offset, &fp_in](const size_t expected_length, const char* section) {
        read_buffer.resize(expected_length);
        const size_t read_length = fread(read_buffer.data(), sizeof(uint8_t), read_buffer.size(), fp_in);
        if (read_length == expected_length) {
            read_offset += expected_length;
            return tcb::span<const uint8_t>(read_buffer);
        }
        throw std::runtime_error(fmt::format(
            "Insufficient bytes while reading {}. Got {} bytes but expected {} bytes",
            section, read_length, expected_length 
        ));
    };

    const auto verify_string = [](tcb::span<const uint8_t> &buf, const std::string& name, const char* section) {
        const char* src_string = reinterpret_cast<const char*>(buf.data());
        const size_t dst_length = name.size();
        if (buf.size() < dst_length) {
            throw std::runtime_error(fmt::format(
                "Not enough bytes for string while reading {}. Got '{:.{}s}' with {} bytes but expected '{}' with {} bytes",
                section, src_string, buf.size(), buf.size(), name, dst_length
            ));
        }
        if (strncmp(src_string, name.data(), dst_length) == 0) {
            buf = buf.subspan(dst_length);
            return;
        }
        throw std::runtime_error(fmt::format(
            "Strings do not match while reading {}. Got '{:.{}s}' but expected '{}'",
            section, src_string, dst_length, name
        ));
    };

    // RIFF chunk
    auto buf = try_read_bytes(4+4+4, "RIFF chunk");
    verify_string(buf, "RIFF", "chunk id");
    const uint32_t riff_chunk_size = read_wav_header_value<uint32_t>(buf);
    verify_string(buf, "WAVE", "wave id");

    // Format chunk
    buf = try_read_bytes(4+4+2+2+4+4+2+2, "Format chunk");
    verify_string(buf, "fmt ", "chunk id");
    const uint32_t fmt_chunk_size = read_wav_header_value<uint32_t>(buf);
    if (fmt_chunk_size != 16 && fmt_chunk_size != 18 && fmt_chunk_size != 40) {
        throw std::runtime_error(fmt::format(
            "Got invalid format chunk size {}, expected one of: [{},{},{}]",
            fmt_chunk_size, 16, 18, 40
        ));
    }
    const uint16_t fmt_code = read_wav_header_value<uint16_t>(buf);
    switch (fmt_code) {
        case 0x0001: { header.format = WavAudioFormat::PCM; break; }
        case 0x0003: { header.format = WavAudioFormat::IEEE754_FLOAT; break; }
        case 0x0006: { header.format = WavAudioFormat::G711_A_LAW; break; }
        case 0x0007: { header.format = WavAudioFormat::G711_MU_LAW; break; }
        case 0xFFFE: { header.format = WavAudioFormat::EXTENSIBLE; break; }
        default: throw std::runtime_error(fmt::format("Got invalid wav audio format code {:04X}", fmt_code));
    }

    header.total_channels = read_wav_header_value<uint16_t>(buf);
    if (header.total_channels != 1 && header.total_channels != 2) {
        throw std::runtime_error(fmt::format("Expected mono or stereo channels but got {} channels", header.total_channels));
    }

    header.samples_per_second = read_wav_header_value<uint32_t>(buf);
    header.average_bytes_per_second = read_wav_header_value<uint32_t>(buf);
    header.data_block_align_bytes = read_wav_header_value<uint16_t>(buf);
    header.bits_per_sample = read_wav_header_value<uint16_t>(buf);

    if (fmt_chunk_size > 16) {
        buf = try_read_bytes(fmt_chunk_size-16, "format chunk extension fields");
        const uint16_t extension_field_size = read_wav_header_value<uint16_t>(buf);
        if (extension_field_size != buf.size()) {
            throw std::runtime_error(fmt::format(
                "Mismatch between expected extension field size {} and actual size {}",
                extension_field_size, buf.size()
            ));
        }
        if (extension_field_size == 22) {
            header.valid_bits_per_sample = read_wav_header_value<uint16_t>(buf);
            header.channel_mask = read_wav_header_value<uint32_t>(buf);

            const uint16_t fmt_code = read_wav_header_value<uint16_t>(buf);
            switch (fmt_code) {
                case 0x0001: { header.format = WavAudioFormat::PCM; break; }
                case 0x0003: { header.format = WavAudioFormat::IEEE754_FLOAT; break; }
                case 0x0006: { header.format = WavAudioFormat::G711_A_LAW; break; }
                case 0x0007: { header.format = WavAudioFormat::G711_MU_LAW; break; }
                case 0xFFFE: {
                    throw std::runtime_error("Got extensible format again in subformat while reading extension fields");
                    break;
                }
                default: throw std::runtime_error(fmt::format("Got invalid wav audio format code {:04X}", fmt_code));
            }
            static const uint8_t REF_GUID[14] = {0x00,0x00,0x00,0x00,0x10,0x00,0x80,0x00,0x00,0xAA,0x00,0x38,0x9B,0x71};
            if (std::memcmp(REF_GUID, buf.data(), 14) != 0) {
                throw std::runtime_error("Extensible format guid does not match reference 14byte prefix");
            }
            header.sub_format = std::string(reinterpret_cast<const char*>(buf.data()), buf.size());
        }
    }

    // Fact chunk for non pcm formats
    if (header.format != WavAudioFormat::PCM) {
        buf = try_read_bytes(4+4, "fact chunk");
        verify_string(buf, "fact", "chunk id");
        const uint32_t fact_chunk_size = read_wav_header_value<uint32_t>(buf);
        if (fact_chunk_size < 4) {
            throw std::runtime_error(fmt::format(
                "Expected fact chunk to have a minimum size of 4 but got {}", fact_chunk_size
            ));
        }
        buf = try_read_bytes(fact_chunk_size, "fact chunk data");
        header.total_samples_per_channel = read_wav_header_value<uint32_t>(buf);
    }

    // Data chunk which stores audio data
    while (true) {
        buf = try_read_bytes(4+4, "possible data chunk");
        const auto chunk_id = std::string(reinterpret_cast<const char*>(buf.data()), 4);
        buf = buf.subspan(4);
        const uint32_t chunk_size = read_wav_header_value<uint32_t>(buf);
        // skip over non-data chunks
        if (strncmp(chunk_id.data(), "data", 4) != 0) {
            const int rv = fseek(fp_in, long(chunk_size), SEEK_CUR);
            const bool is_seek_success = rv == 0;
            if (!is_seek_success) {
                throw std::runtime_error(fmt::format(
                    "Failed to skip over non data chunk '{}' that is {} bytes", chunk_id, chunk_size
                ));
            }
            read_offset += chunk_size;
            header.unhandled_chunks.push_back({
                std::move(chunk_id),
                chunk_size,
            });
            continue;
        }
        header.data_chunk_size = chunk_size;
        header.data_chunk_offset = read_offset;
        break;
    }
    return header;
}

class WavFileReader: public InputBuffer<float>
{
private:
    std::shared_ptr<FileWrapper> m_file;
    const WavHeader m_header;
    std::function<size_t(tcb::span<float>)> m_parser = nullptr;
    std::vector<uint8_t> m_buffer;
    size_t m_total_read = 0;
public:
    WavFileReader(std::shared_ptr<FileWrapper> file)
    : m_file(file), m_header(wav_read_header(file->get_handle()))
    {
        switch (m_header.format) {
            case WavAudioFormat::PCM: {
                switch (m_header.bits_per_sample) {
                    case 8: {
                        m_parser = [this](tcb::span<float> dest) -> size_t {
                            const auto src = read_bytes(dest.size());
                            const size_t N = src.size();
                            constexpr float BIAS = float(std::numeric_limits<uint8_t>::max())/2.0f;
                            constexpr float SCALE = 1.0f/BIAS;
                            for (size_t i = 0; i < N; i++) {
                                dest[i] = (float(src[i])-BIAS)*SCALE;
                            }
                            return N;
                        };
                        break;
                    }
                    case 16: {
                        m_parser = [this](tcb::span<float> dest) -> size_t {
                            constexpr size_t stride = 2;
                            const auto src = read_bytes(dest.size()*stride);
                            const size_t N = src.size()/stride;
                            constexpr float SCALE = 1.0f/float(std::numeric_limits<int16_t>::max());
                            for (size_t i = 0; i < N; i++) {
                                const size_t j = i*stride;
                                const int16_t value = int16_t(src[j]) | int16_t(src[j+1]) << 8;
                                dest[i] = float(value)*SCALE;
                            }
                            return N;
                        };
                        break;
                    }
                    case 24: {
                        m_parser = [this](tcb::span<float> dest) -> size_t {
                            constexpr size_t stride = 3;
                            const auto src = read_bytes(dest.size()*stride);
                            const size_t N = src.size()/stride;
                            constexpr float SCALE = 1.0f/float(int32_t(0x7F'FF'FF));
                            for (size_t i = 0; i < N; i++) {
                                const size_t j = i*stride;
                                int32_t value = int32_t(src[j]) | int32_t(src[j+1]) << 8 | int32_t(src[j+2]) << 16;
                                // sign extend 24bit negative to 32bit negative for 2's complement
                                if (value & 0x80'00'00) value |= 0xFF'00'00'00;
                                dest[i] = float(value)*SCALE;
                            }
                            return N;
                        };
                        break;
                    }
                    case 32: {
                        m_parser = [this](tcb::span<float> dest) -> size_t {
                            constexpr size_t stride = 4;
                            const auto src = read_bytes(dest.size()*stride);
                            const size_t N = src.size()/stride;
                            constexpr float SCALE = 1.0f/float(std::numeric_limits<int32_t>::max());
                            for (size_t i = 0; i < N; i++) {
                                const size_t j = i*stride;
                                const int32_t value =
                                    int32_t(src[j]) |
                                    int32_t(src[j+1]) << 8 |
                                    int32_t(src[j+2]) << 16 |
                                    int32_t(src[j+3]) << 24;
                                dest[i] = float(value)*SCALE;
                            }
                            return N;
                        };
                        break;
                    }
                    default: {
                        throw std::runtime_error(fmt::format(
                            "Unhandled PCM format with {} bits per sample", m_header.bits_per_sample
                        ));
                    }
                }
                break;
            }
            case WavAudioFormat::IEEE754_FLOAT: {
                switch (m_header.bits_per_sample) {
                    case 32: {
                        m_parser = [this](tcb::span<float> dest) -> size_t {
                            constexpr size_t stride = 4;
                            const auto src = read_bytes(dest.size()*stride);
                            const size_t N = src.size()/stride;
                            for (size_t i = 0; i < N; i++) {
                                const size_t j = i*stride;
                                union {
                                    uint32_t u32;
                                    float f32;
                                } value;
                                value.u32 =
                                    uint32_t(src[j]) |
                                    uint32_t(src[j+1]) << 8 |
                                    uint32_t(src[j+2]) << 16 |
                                    uint32_t(src[j+3]) << 24;
                                dest[i] = value.f32;
                            }
                            return N;
                        };
                        break;
                    }
                    case 64: {
                        m_parser = [this](tcb::span<float> dest) -> size_t {
                            constexpr size_t stride = 8;
                            const auto src = read_bytes(dest.size()*stride);
                            const size_t N = src.size()/stride;
                            for (size_t i = 0; i < N; i++) {
                                const size_t j = i*stride;
                                union {
                                    uint64_t u64;
                                    double f64;
                                } value;
                                value.u64 =
                                    uint64_t(src[j]) |
                                    uint64_t(src[j+1]) << 8 |
                                    uint64_t(src[j+2]) << 16 |
                                    uint64_t(src[j+3]) << 24 |
                                    uint64_t(src[j+4]) << 32 |
                                    uint64_t(src[j+5]) << 40 |
                                    uint64_t(src[j+6]) << 48 |
                                    uint64_t(src[j+7]) << 56;
                                dest[i] = float(value.f64);
                            }
                            return N;
                        };
                        break;
                    }
                    default: {
                        throw std::runtime_error(fmt::format(
                            "Unhandled IEEE754 format with {} bits per sample", m_header.bits_per_sample
                        ));
                    }
                }
                break;
            }
            case WavAudioFormat::G711_A_LAW: {
                if (m_header.bits_per_sample != 8) {
                    throw std::runtime_error(fmt::format(
                        "Unhandled G711 A law format with {} bits per sample, expected 8 bits", m_header.bits_per_sample
                    ));
                }
                m_parser = [this](tcb::span<float> dest) -> size_t {
                    const auto src = read_bytes(dest.size());
                    const size_t N = src.size();
                    for (size_t i = 0; i < N; i++) {
                        // https://en.wikipedia.org/wiki/G.711#A-law
                        uint8_t value = src[i];
                        value ^= 0b01010101;
                        const int16_t sign = int16_t((value >> 7) ^ 0b1); // invert sign bit
                        const uint8_t exponent = (value >> 4) & 0b111;
                        const int16_t mantissa = int16_t(value & 0b1111);
                        int16_t decoded_value = 0; // 13bit signed
                        decoded_value = (mantissa << 1) | 0b1;
                        if (exponent > 0) decoded_value |= 0b1 << 5;
                        if (exponent > 1) decoded_value <<= exponent-1;
                        if (sign) decoded_value ^= 0xFFFF;
                        constexpr float SCALE = 1.0f/float(int16_t(0x1000));
                        dest[i] = float(decoded_value)*SCALE;
                    }
                    return N;
                };
                break;
            }
            case WavAudioFormat::G711_MU_LAW: {
                if (m_header.bits_per_sample != 8) {
                    throw std::runtime_error(fmt::format(
                        "Unhandled G711 mu law format with {} bits per sample, expected 8 bits", m_header.bits_per_sample
                    ));
                }
                m_parser = [this](tcb::span<float> dest) -> size_t {
                    const auto src = read_bytes(dest.size());
                    const size_t N = src.size();
                    for (size_t i = 0; i < N; i++) {
                        // https://en.wikipedia.org/wiki/G.711#%CE%BC-law
                        uint8_t value = src[i];
                        value ^= 0b11111111;
                        const int16_t sign = int16_t(value >> 7);
                        const uint8_t exponent = (value >> 4) & 0b111;
                        const int16_t mantissa = int16_t(value & 0b1111);
                        // 14bit signed
                        int16_t decoded_value =  ((0b1 << 5) | (mantissa << 1) | 0b1) << exponent;
                        if (sign) decoded_value ^= 0xFFFF;
                        constexpr float SCALE = 1.0f/float(int16_t(0x2000));
                        dest[i] = float(decoded_value)*SCALE;
                    }
                    return N;
                };
                break;
            }
            case WavAudioFormat::EXTENSIBLE: {
                throw std::runtime_error("Unhandled extensible wav audio format is not supported");
            }
            default: {
                throw std::runtime_error(fmt::format(
                    "Unsupported wav header format with code {:X}", static_cast<uint16_t>(m_header.format)
                ));
            }
        }
    }
    ~WavFileReader() override = default;
    tcb::span<uint8_t> read_bytes(size_t length) {
        if (m_total_read >= m_header.data_chunk_size) return {};
        const size_t remaining_bytes = m_header.data_chunk_size - m_total_read;
        length = (remaining_bytes >= length) ? length : remaining_bytes;
        m_buffer.resize(length);
        const auto dest = tcb::span(m_buffer);
        const size_t total_read = m_file->read(dest);
        return dest.first(total_read);
    }
    size_t read(tcb::span<float> dest) override {
        if (m_parser == nullptr) return 0;
        return m_parser(dest);
    }
    bool loop() {
        const bool is_success = m_file->seek(long(m_header.data_chunk_offset), FileWrapper::SeekMode::START);
        if (is_success) m_total_read = 0;
        return is_success;
    }
    const auto& get_header() const { return m_header; }
};
