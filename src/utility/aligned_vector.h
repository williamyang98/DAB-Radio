#pragma once

#include <stdint.h>
#include <stddef.h>
#include <assert.h>
#include <new>
#include <cstring>

// Wrap aligned allocator and deallocator calls in a RAII class
template <typename T>
class AlignedVector 
{
private:
    T* m_buf;
    size_t m_len;
    size_t m_align;
public:
    explicit AlignedVector(const size_t len=0, const size_t align=32) {
        // Cannot align to zero bytes
        assert(align != 0u);
        m_len = len;
        m_align = align;
        m_buf = NULL;
        const size_t total_bytes = m_len*sizeof(T);
        if (m_len > 0) {
            m_buf = reinterpret_cast<T*>(operator new[](total_bytes, std::align_val_t(m_align)));
        }
    }

    ~AlignedVector() {
        if (m_buf != NULL) {
            operator delete[](m_buf, std::align_val_t(m_align));
        }
        m_len = 0;
        m_align = 0;
        m_buf = NULL;
    }

    AlignedVector<T>(const AlignedVector<T>& other) {
        m_len = other.size();
        m_align = other.alignment();
        m_buf = NULL;

        const size_t total_bytes = m_len*sizeof(T);
        if (m_len > 0) {
            m_buf = reinterpret_cast<T*>(operator new[](total_bytes, std::align_val_t(m_align)));
            std::memcpy(m_buf, other.data(), total_bytes);
        }
    };

    AlignedVector<T>& operator=(const AlignedVector<T>& other) {
        if (this == &other) {
            return *this;
        }

        if (m_buf != NULL) {
            operator delete[](m_buf, std::align_val_t(m_align));
        }

        m_len = other.size();
        m_align = other.alignment();
        m_buf = NULL;

        const size_t total_bytes = m_len*sizeof(T);
        if (m_len > 0) {
            m_buf = reinterpret_cast<T*>(operator new[](total_bytes, std::align_val_t(m_align)));
            std::memcpy(m_buf, other.data(), total_bytes);
        }

        return *this;
    }

    AlignedVector<T>(AlignedVector<T>&& other) {
        m_len = other.m_len;
        m_align = other.m_align;
        m_buf = other.m_buf;

        other.m_len = 0;
        other.m_align = 0;
        other.m_buf = NULL;
    }

    AlignedVector<T>& operator=(AlignedVector<T>&& other) {
        if (this == &other) {
            return *this;
        }

        if (m_buf != NULL) {
            operator delete[](m_buf, std::align_val_t(m_align));
        }

        m_len = other.m_len;
        m_align = other.m_align;
        m_buf = other.m_buf;

        other.m_len = 0;
        other.m_align = 0;
        other.m_buf = NULL;

        return *this;
    };

    inline
    T& operator[](size_t index) {
        return m_buf[index];
    }

    inline
    const T& operator[](size_t index) const {
        return m_buf[index];
    }

    auto begin() const { return m_buf; }
    auto end() const { return &m_buf[m_len]; }
    auto size() const { return m_len; }
    auto data() const { return m_buf; }
    auto alignment() const { return m_align; }
};