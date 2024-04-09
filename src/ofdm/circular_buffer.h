#pragma once

#include <stddef.h>
#include "utility/span.h"

template <typename T>
class CircularBuffer
{
private:
    tcb::span<T>& m_buf;
    size_t m_index;
    size_t m_length;
public:
    explicit CircularBuffer(tcb::span<T>& buf)
    : m_buf(buf), m_index(0), m_length(0) {}
    // Read the data from a source buffer and append it to this buffer
    // We can forcefully read all the data
    size_t ConsumeBuffer(tcb::span<const T> src, const bool read_all=false) {
        const size_t N = src.size();
        const size_t capacity = Capacity();
        size_t nb_read;
        if (read_all) {
            nb_read = N;
        } else {
            const size_t N_remain = capacity-m_length;
            nb_read = (N > N_remain) ? N_remain : N;
        }

        for (size_t i = 0; i < nb_read; i++) {
            m_buf[m_index++] = src[i];
            m_index = m_index % capacity;
        }
        m_length += nb_read;
        if (m_length > capacity) {
            m_length = capacity;
        }
        return nb_read;
    }
    // index the circular buffer with wrap-around
    auto begin() const { return m_buf.begin(); }
    auto end() const { return m_buf.end(); }
    size_t size() const { return m_buf.size(); }
    auto data() const { return m_buf.data(); }
    T& operator[](size_t i) { return m_buf[i % m_buf.size()]; }

    void Reset() {
        m_length = 0;
        m_index = 0;
    }
    void SetLength(size_t N) { m_length = N; }
    size_t Length() const { return m_length; }
    size_t Capacity() const { return m_buf.size(); }
    size_t GetIndex() const { return m_index; }
    bool IsEmpty() const { return m_length == 0; }
    bool IsFull() const { return m_length == Capacity(); }
};