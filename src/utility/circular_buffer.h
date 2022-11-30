#pragma once

#include "utility/span.h"

template <typename T>
class CircularBuffer
{
private:
    tcb::span<T>& buf;
    size_t index;
    size_t length;
public:
    CircularBuffer(tcb::span<T>& _buf)
    : buf(_buf), index(0), length(0) {}
    // Read the data from a source buffer and append it to this buffer
    // We can forcefully read all the data
    size_t ConsumeBuffer(tcb::span<const T> src, const bool read_all=false) {
        const size_t N = src.size();
        const size_t capacity = Capacity();
        size_t nb_read;
        if (read_all) {
            nb_read = N;
        } else {
            const size_t N_remain = capacity-length;
            nb_read = (N > N_remain) ? N_remain : N;
        }

        for (int i = 0; i < nb_read; i++) {
            buf[index++] = src[i];
            index = index % capacity;
        }
        length += nb_read;
        if (length > capacity) {
            length = capacity;
        }
        return nb_read;
    }
    // index the circular buffer with wrap-around
    auto begin() const { return buf.begin(); }
    auto end() const { return buf.end(); }
    size_t size() const { return buf.size(); }
    auto data() const { return buf.data(); }
    T& operator[](size_t i) { return buf[i % buf.size()]; }

    void Reset() {
        length = 0;
        index = 0;
    }
    void SetLength(size_t N) { length = N; }
    size_t Length() const { return length; }
    size_t Capacity() const { return buf.size(); }
    size_t GetIndex() const { return index; }
    bool IsEmpty() const { return length == 0; }
    bool IsFull() const { return length == Capacity(); }
};