#pragma once

#include <vector>
#include "utility/span.h"

template <typename T>
class CircularBuffer
{
private:
    std::vector<T> buf;
    size_t capacity;
    size_t length;
    size_t index;
public:
    CircularBuffer(const size_t _N=0)
    : capacity(_N), buf(_N) {
        length = 0;
        index = 0;
    }
    // Read the data from a source buffer and append it to this buffer
    // We can forcefully read all the data
    size_t ConsumeBuffer(tcb::span<const T> src, const bool read_all=false) {
        const size_t N = src.size();
        size_t nb_read;
        if (read_all) {
            nb_read = N;
        } else {
            const size_t N_remain = capacity-length;
            nb_read = (N > N_remain) ? N_remain : N;
        }

        for (size_t i = 0; i < nb_read; i++) {
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
    T& operator[](size_t i) { return buf[i % capacity]; }
    void Reset() {
        length = 0;
        index = 0;
    }
    void Resize(const size_t _capacity) {
        capacity = _capacity;
        length = (length > capacity) ? capacity : length;
        index = (index % capacity);
        buf.resize(capacity);
    }
    void SetLength(size_t N) { length = N; }
    size_t Length() const { return length; }
    size_t Capacity() const { return capacity; }
    size_t GetIndex() const { return index; }
    bool IsEmpty() const { return length == 0; }
    bool IsFull() const { return length == capacity; }
};