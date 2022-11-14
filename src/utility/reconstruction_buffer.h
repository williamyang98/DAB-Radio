#pragma once

#include <vector> 
#include "utility/span.h"

// reconstruct a block of size M from blocks of size N
template <typename T>
class ReconstructionBuffer 
{
private:
    std::vector<T> buf;
    size_t capacity;
    size_t length;
public:
    ReconstructionBuffer(const size_t _N=0)
    : capacity(_N), length(0), buf(_N) {}
    // Read the data from a source buffer and append it to this buffer
    size_t ConsumeBuffer(tcb::span<const T> src) {
        const size_t N = src.size();
        const size_t N_required = capacity-length;
        const size_t nb_read = (N_required >= N) ? N : N_required;
        for (int i = 0; i < nb_read; i++) {
            buf[length++] = src[i];
        }
        return nb_read;
    }
    void Resize(const size_t _capacity) {
        capacity = _capacity;
        length = (length > capacity) ? capacity : length;
        buf.resize(capacity);
    }
    T& operator[](size_t i) { return buf[i]; }
    tcb::span<T> GetData() { return buf; }
    void Reset() { length = 0; };
    void SetLength(size_t N) { length = N; }
    size_t Length() const { return length; }
    size_t Capacity() const { return capacity; }
    bool IsEmpty() const { return length == 0; }
    bool IsFull() const { return length == capacity; }
};