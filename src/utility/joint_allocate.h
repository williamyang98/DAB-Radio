#pragma once

#include <stdint.h>
#include <memory>
#include "span.h"

std::unique_ptr<uint8_t[]> AllocateJoint(size_t curr_size) {
    return std::make_unique<uint8_t[]>(curr_size);
}

template <typename T, typename ... Ts>
std::unique_ptr<uint8_t[]> AllocateJoint(size_t curr_size, tcb::span<T>& buf, size_t len, Ts&& ... args) {
    const auto align_elem_size = alignof(T);
    const int64_t align_size = ((curr_size+align_elem_size-1) / align_elem_size) * align_elem_size;

    const size_t buf_size = len*sizeof(T);
    const size_t new_size = align_size + buf_size;
    auto data = AllocateJoint(new_size, args...);
    buf = tcb::span<T>(reinterpret_cast<T*>(&data[align_size]), len);
    return data;
}

// Recursive template for creating a jointly allocated block 
template <typename T, typename ... Ts>
std::unique_ptr<uint8_t[]> AllocateJoint(tcb::span<T>& buf, size_t len, Ts&& ... args) {
    return AllocateJoint(0, buf, len, args...);
}