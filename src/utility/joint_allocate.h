#pragma once

#include <stdint.h>
#include <new>
#include <assert.h>
#include "span.h"

// Since we need a corresponding aligned deallocator call wrap this in a RAII class
// NOTE: Move and move assign operators are not thread safe 
class AlignedBlock 
{
private:
    uint8_t* buf;
    size_t len;
    size_t align;
public:
    AlignedBlock(size_t _len=0, size_t _align=0) 
    {
        len = _len;
        align = _align;
        buf = NULL;
        if (len > 0) {
            buf = (uint8_t*)operator new[](len, std::align_val_t(align));
        }
    }
    ~AlignedBlock() {
        if (buf) {
            operator delete[](buf, std::align_val_t(align));
        }
    }
    AlignedBlock(const AlignedBlock&) = delete;
    AlignedBlock& operator=(const AlignedBlock&) = delete;
    // move constructor so we can use RVO
    AlignedBlock(AlignedBlock&& other) {
        len = other.len;
        align = other.align;
        buf = other.buf;

        other.len = 0;
        other.align = 0;
        other.buf = NULL;
    }
    // move assign operator so we can set by value
    AlignedBlock& operator=(AlignedBlock&& other) {
        if (buf) {
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
    uint8_t& operator[](size_t index) {
        return buf[index];
    }
    auto begin() const { return buf; }
    auto end() const { return buf + len; }
    auto size() const { return len; }
    auto data() const { return buf; }
};

// Define the specification for each buffer inside the joint block
struct BufferParameters {
    size_t length;
    size_t alignment;
    // By default we align buffer to the size of the type
    BufferParameters(size_t _length, size_t _alignment=0)
    : length(_length), alignment(_alignment) {}
};

static AlignedBlock AllocateJoint(size_t curr_size, size_t align_size) {
    return AlignedBlock(curr_size, align_size);
}

template <typename T, typename ... Ts>
static AlignedBlock AllocateJoint(size_t curr_joint_size, size_t joint_alignment, tcb::span<T>& buf, BufferParameters params, Ts&& ... args) {
    // create padding so that next buffer has its alignment
    const auto align_elem_size = params.alignment ? params.alignment : sizeof(T);
    const size_t padded_joint_size = ((curr_joint_size+align_elem_size-1) / align_elem_size) * align_elem_size;

    // joint block must be aligned to the most demanding buffer alignment
    joint_alignment = (align_elem_size > joint_alignment) ? align_elem_size : joint_alignment;

    // get offset into our aligned and padded block
    const size_t buf_size = params.length*sizeof(T);
    const size_t new_joint_size = padded_joint_size + buf_size;
    auto data = AllocateJoint(new_joint_size, joint_alignment, args...);

    const auto data_offset = tcb::span(data).subspan(padded_joint_size, buf_size);

    // Check that alignment was met
    const auto addr_val = reinterpret_cast<uintptr_t>(data_offset.data());
    const bool is_aligned = (addr_val % align_elem_size) == 0;
    assert(is_aligned);

    buf = tcb::span<T>(reinterpret_cast<T*>(data_offset.data()), params.length);
    return data;
}

// Recursive template for creating a jointly allocated block 
template <typename T, typename ... Ts>
static AlignedBlock AllocateJoint(tcb::span<T>& buf, BufferParameters params, Ts&& ... args) {
    return AllocateJoint(0, 1, buf, params, args...);
}