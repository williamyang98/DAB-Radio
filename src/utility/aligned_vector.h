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
    T* buf;
    size_t len;
    size_t align;
public:
    explicit AlignedVector(const size_t _len=0, const size_t _align=32) {
        // Cannot align to zero bytes
        assert(_align != 0u);
        len = _len;
        align = _align;
        buf = NULL;
        const size_t total_bytes = len*sizeof(T);
        if (len > 0) {
            buf = reinterpret_cast<T*>(operator new[](total_bytes, std::align_val_t(align)));
        }
    }

    ~AlignedVector() {
        if (buf != NULL) {
            operator delete[](buf, std::align_val_t(align));
        }
        len = 0;
        align = 0;
        buf = NULL;
    }

    AlignedVector<T>(const AlignedVector<T>& other) {
        len = other.size();
        align = other.alignment();
        buf = NULL;

        const size_t total_bytes = len*sizeof(T);
        if (len > 0) {
            buf = reinterpret_cast<T*>(operator new[](total_bytes, std::align_val_t(align)));
            std::memcpy(buf, other.data(), total_bytes);
        }
    };

    AlignedVector<T>& operator=(const AlignedVector<T>& other) {
        if (this == &other) {
            return *this;
        }

        if (buf != NULL) {
            operator delete[](buf, std::align_val_t(align));
        }

        len = other.size();
        align = other.alignment();
        buf = NULL;

        const size_t total_bytes = len*sizeof(T);
        if (len > 0) {
            buf = reinterpret_cast<T*>(operator new[](total_bytes, std::align_val_t(align)));
            std::memcpy(buf, other.data(), total_bytes);
        }

        return *this;
    }

    AlignedVector<T>(AlignedVector<T>&& other) {
        len = other.len;
        align = other.align;
        buf = other.buf;

        other.len = 0;
        other.align = 0;
        other.buf = NULL;
    }

    AlignedVector<T>& operator=(AlignedVector<T>&& other) {
        if (this == &other) {
            return *this;
        }

        if (buf != NULL) {
            operator delete[](buf, std::align_val_t(align));
        }

        len = other.len;
        align = other.align;
        buf = other.buf;

        other.len = 0;
        other.align = 0;
        other.buf = NULL;

        return *this;
    };

    inline
    T& operator[](size_t index) {
        return buf[index];
    }

    inline
    const T& operator[](size_t index) const {
        return buf[index];
    }

    auto begin() const { return buf; }
    auto end() const { return &buf[len]; }
    auto size() const { return len; }
    auto data() const { return buf; }
    auto alignment() const { return align; }
};