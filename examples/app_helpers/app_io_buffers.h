#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <vector>
#include "utility/span.h"
#include "../audio/ring_buffer.h"

template <typename T>
struct InputBuffer {
    virtual ~InputBuffer() {}
    virtual size_t read(tcb::span<T> dest) = 0;
};

template <typename T>
struct OutputBuffer {
    virtual ~OutputBuffer() {}
    virtual size_t write(tcb::span<const T> src) = 0;
};

template <typename T, typename U>
class ReinterpretCastInputBuffer: public InputBuffer<T>
{
private:
    std::shared_ptr<InputBuffer<U>> m_input = nullptr;
public:
    ReinterpretCastInputBuffer(std::shared_ptr<InputBuffer<U>> input)
    : m_input(input) 
    {
        static_assert(
            sizeof(T) % sizeof(U) == 0 || sizeof(U) % sizeof(T) == 0,
            "Converted type must be a multiple/divisor of original type"
        );
    }
    ~ReinterpretCastInputBuffer() override = default;
    size_t read(tcb::span<T> dest) override {
        if (m_input == nullptr) return 0;
        if constexpr (sizeof(T) >= sizeof(U)) {
            constexpr size_t stride = sizeof(T)/sizeof(U);
            const auto converted_dest = tcb::span<U>(
                reinterpret_cast<U*>(dest.data()),
                dest.size()*stride
            );
            const size_t length = m_input->read(converted_dest);
            return length/stride;
        } else {
            constexpr size_t stride = sizeof(U)/sizeof(T);
            const auto converted_dest = tcb::span<U>(
                reinterpret_cast<U*>(dest.data()),
                dest.size()/stride
            );
            const size_t length = m_input->read(converted_dest);
            return length*stride;
        }
    }
};

template <typename T, typename U>
class StaticCastInputBuffer: public InputBuffer<T>
{
private:
    std::shared_ptr<InputBuffer<U>> m_input = nullptr;
    std::vector<U> m_buffer;
public:
    StaticCastInputBuffer(std::shared_ptr<InputBuffer<U>> input): m_input(input) {}
    ~StaticCastInputBuffer() override = default;
    size_t read(tcb::span<T> dest) override {
        m_buffer.resize(dest.size());
        const size_t length = m_input->read(m_buffer);
        for (size_t i = 0; i < length; i++) {
            dest[i] = static_cast<T>(m_buffer[i]);
        }
        return length;
    }
};

template <typename T, typename U>
class ReinterpretCastOutputBuffer: public OutputBuffer<T>
{
private:
    std::shared_ptr<OutputBuffer<U>> m_output = nullptr;
public:
    ReinterpretCastOutputBuffer(std::shared_ptr<OutputBuffer<U>> output)
    : m_output(output)
    {
        static_assert(sizeof(T) % sizeof(U) == 0, "Converted type must be a multiple of original type");
    }
    ~ReinterpretCastOutputBuffer() override = default;
    size_t write(tcb::span<const T> src) override {
        if (m_output == nullptr) return 0;
        constexpr size_t stride = sizeof(T)/sizeof(U);
        const auto converted_src = tcb::span<const U>(
            reinterpret_cast<const U*>(src.data()),
            src.size()*stride
        );
        const size_t length = m_output->write(converted_src);
        return length/stride;
    }
};

class FileWrapper {
private:
    FILE* m_file = nullptr;
    std::shared_mutex m_mutex;
public:
    enum class SeekMode {
        START,
        CURRENT,
        END,
    };
public:
    explicit FileWrapper(FILE* file): m_file(file) {}
    virtual ~FileWrapper() { close(); }
    void close() {
        auto lock = std::unique_lock(m_mutex);
        if (m_file != nullptr) {
            fclose(m_file);
            m_file = nullptr;
        }
    }
    template <typename T>
    size_t write(tcb::span<const T> src) {
        auto lock = std::shared_lock(m_mutex);
        if (m_file == nullptr) return 0;
        return fwrite(src.data(), sizeof(T), src.size(), m_file);
    }
    template <typename T>
    size_t read(tcb::span<T> dest) {
        auto lock = std::shared_lock(m_mutex);
        if (m_file == nullptr) return 0;
        return fread(dest.data(), sizeof(T), dest.size(), m_file);
    }
    bool seek(const long offset, const SeekMode mode) {
        auto lock = std::shared_lock(m_mutex);
        if (m_file == nullptr) return false;
        int mode_id = SEEK_SET;
        switch (mode) {
        case SeekMode::START: mode_id = SEEK_SET; break;
        case SeekMode::CURRENT: mode_id = SEEK_CUR; break;
        case SeekMode::END: mode_id = SEEK_END; break;
        default: break;
        }
        const int rv = fseek(m_file, offset, mode_id);
        return rv == 0;
    }
    FILE* get_handle() const { return m_file; }
};

template <typename T>
class InputFile: public InputBuffer<T>, public FileWrapper {
private:
public:
    explicit InputFile(FILE* file): FileWrapper(file) {}
    ~InputFile() override = default; 
    size_t read(tcb::span<T> dest) override {
        return FileWrapper::read(dest);
    }
};

template <typename T>
class OutputFile: public OutputBuffer<T>, public FileWrapper {
public:
    explicit OutputFile(FILE* file): FileWrapper(file) {}
    ~OutputFile() override = default;
    size_t write(tcb::span<const T> src) override {
        return FileWrapper::write(src);
    }
};

template <typename T>
class InputOutputFile: public InputBuffer<T>, public OutputBuffer<T>, public FileWrapper {
public:
    explicit InputOutputFile(FILE* file): FileWrapper(file) {}
    ~InputOutputFile() override = default;
    size_t read(tcb::span<T> dest) override {
        return FileWrapper::read(dest);
    }
    size_t write(tcb::span<const T> src) override {
        return FileWrapper::write(src);
    }
};

// single producer single consumer ring buffer
template <typename T>
class ThreadedRingBuffer: public InputBuffer<T>, public OutputBuffer<T>
{
private:
    RingBuffer<T> m_ring_buffer;
    std::mutex m_mutex_ring_buffer;
    std::condition_variable m_cv_reader;
    std::condition_variable m_cv_writer;
    bool m_is_closed = false;
public:
    explicit ThreadedRingBuffer(size_t length): m_ring_buffer(length) {}
    ~ThreadedRingBuffer() override {
        close();
    }

    void close() {
        {
            auto lock = std::unique_lock(m_mutex_ring_buffer);
            m_is_closed = true;
        }
        m_cv_reader.notify_all();
        m_cv_writer.notify_all();
    }

    size_t read(tcb::span<T> dest) override {
        auto lock = std::unique_lock(m_mutex_ring_buffer);
        size_t total_written = 0;
        while (true) {
            const size_t length = m_ring_buffer.read(dest);
            total_written += length;
            dest = dest.subspan(length);
            if (length > 0) m_cv_reader.notify_one();
            if (dest.empty()) break;
            m_cv_writer.wait(lock, [this](){
                return m_is_closed || !m_ring_buffer.is_empty();
            });
            if (m_is_closed) break;
        }
        return total_written;
    }

    size_t write(tcb::span<const T> src) override {
        auto lock = std::unique_lock(m_mutex_ring_buffer);
        size_t total_read = 0;
        while (true) {
            const size_t length = m_ring_buffer.write(src);
            total_read += length;
            src = src.subspan(length);
            if (length > 0) m_cv_writer.notify_one();
            if (src.empty()) break;
            m_cv_reader.wait(lock, [this](){
                return m_is_closed || !m_ring_buffer.is_full();
            });
            if (m_is_closed) break;
        }
        return total_read;
    }
};

template <typename T>
class OutputSplitter: public OutputBuffer<T> {
private:
    std::vector<std::shared_ptr<OutputBuffer<T>>> m_buffers;
public:
    ~OutputSplitter() override = default;
    void add_output_stream(std::shared_ptr<OutputBuffer<T>> buffer) {
        m_buffers.push_back(buffer);
    }
    size_t write(tcb::span<const T> src) override {
        size_t max_length = 0;
        for (auto& buffer: m_buffers) {
            const size_t length = buffer->write(src);
            if (length > max_length) {
                max_length = length;
            }
        }
        return max_length;
    };
};

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

static bool get_is_machine_little_endian() {
    volatile union {
        uint32_t value;
        uint8_t data[4];
    } e;
    e.value = 0x00000001;
    return e.data[0] == 0x01;
}

template <typename T>
class ReverseEndian: public InputBuffer<T>
{
private:
    std::shared_ptr<InputBuffer<T>> m_input = nullptr;
public:
    ReverseEndian(std::shared_ptr<InputBuffer<T>> input): m_input(input) {}
    ~ReverseEndian() override = default;
    size_t read(tcb::span<T> dest) override {
        if (m_input == nullptr) return 0;
        const size_t length = m_input->read(dest);
        reverse_endian_inplace(dest.first(length));
        return length;
    }
};

