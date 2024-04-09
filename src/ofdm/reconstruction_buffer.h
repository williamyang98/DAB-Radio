#pragma once

#include <stddef.h>
#include "utility/span.h"

// reconstruct a block of size M from blocks of size N
template <typename T>
class ReconstructionBuffer 
{
private:
    tcb::span<T>& m_buf;
    size_t m_length;
public:
    explicit ReconstructionBuffer(tcb::span<T>& buf)
    : m_buf(buf), m_length(0) {}
    // Read the data from a source buffer and append it to this buffer
    size_t ConsumeBuffer(tcb::span<const T> src) {
        const size_t N = src.size();
        const size_t N_required = Capacity()-m_length;
        const size_t nb_read = (N_required >= N) ? N : N_required;
        auto wr_buf = m_buf.subspan(m_length, nb_read);
        for (size_t i = 0; i < nb_read; i++) {
            wr_buf[i] = src[i];
        }
        m_length += nb_read;
        return nb_read;
    }

    auto begin() const { return m_buf.begin(); }
    auto end() const { return m_buf.end(); }
    size_t size() const { return m_buf.size(); }
    auto data() const { return m_buf.data(); }
    T& operator[](size_t i) { return m_buf[i]; }

    void Reset() { m_length = 0; };
    void SetLength(size_t N) { m_length = N; }
    size_t Length() const { return m_length; }
    size_t Capacity() const { return m_buf.size(); }
    bool IsEmpty() const { return m_length == 0; }
    bool IsFull() const { return m_length == Capacity(); }
};