#pragma once

#include "utility/span.h"

// reconstruct a block of size M from blocks of size N
template <typename T>
class ReconstructionBuffer 
{
private:
    tcb::span<T>& buf;
    size_t length;
public:
    ReconstructionBuffer(tcb::span<T>& _buf)
    : buf(_buf), length(0) {}
    // Read the data from a source buffer and append it to this buffer
    size_t ConsumeBuffer(tcb::span<const T> src) {
        const size_t N = src.size();
        const size_t N_required = Capacity()-length;
        const size_t nb_read = (N_required >= N) ? N : N_required;
        for (int i = 0; i < nb_read; i++) {
            buf[length++] = src[i];
        }
        return nb_read;
    }

    auto begin() const { return buf.begin(); }
    auto end() const { return buf.end(); }
    size_t size() const { return buf.size(); }
    auto data() const { return buf.data(); }
    T& operator[](size_t i) { return buf[i]; }

    void Reset() { length = 0; };
    void SetLength(size_t N) { length = N; }
    size_t Length() const { return length; }
    size_t Capacity() const { return buf.size(); }
    bool IsEmpty() const { return length == 0; }
    bool IsFull() const { return length == Capacity(); }
};