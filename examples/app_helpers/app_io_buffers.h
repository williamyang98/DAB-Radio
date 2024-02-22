#pragma once

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

class FileWrapper {
private:
    FILE* m_file = nullptr;
    std::shared_mutex m_mutex;
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
