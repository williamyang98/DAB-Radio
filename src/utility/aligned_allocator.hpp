#pragma once

#include <assert.h>
#include <cstddef>
#include <new>
#include <type_traits>

// C++17 aligned allocator
// Sources: https://en.cppreference.com/w/cpp/named_req/Allocator
//          https://en.cppreference.com/w/cpp/memory/allocator_traits
//          https://en.cppreference.com/w/cpp/memory/new/operator_new
template <typename T>
class AlignedAllocator 
{
public:
    using value_type = T;
    using is_always_equal = std::false_type;
    using propagate_on_container_copy_assignment = std::false_type; // do not copy allocator when copying object
    using propagate_on_container_move_assignment = std::true_type; // move allocator when moving object
    using propagate_on_container_swap = std::true_type;
    template <typename U>
    struct rebind {
        using other = AlignedAllocator<U>;
    };
    static constexpr size_t default_alignment = sizeof(std::size_t);
private:
    std::size_t m_alignment;
public:
    explicit AlignedAllocator(std::size_t alignment=default_alignment): m_alignment(alignment) {}
    [[nodiscard]] T* allocate(std::size_t length) {
        return reinterpret_cast<T*>(operator new(length*sizeof(T), std::align_val_t(m_alignment)));
    }
    void deallocate(T* const ptr, std::size_t /*length*/) const {
        assert(reinterpret_cast<uintptr_t>(ptr) % m_alignment == 0);
        operator delete(ptr, std::align_val_t(m_alignment));
    }
    bool operator==(const AlignedAllocator& other) const noexcept {
        return m_alignment == other.m_alignment;
    }
    bool operator!=(const AlignedAllocator& other) const noexcept {
        return m_alignment != other.m_alignment;
    }
    // cast between types
    inline size_t get_alignment() const { return m_alignment; }
    template <typename U>
    explicit AlignedAllocator(const AlignedAllocator<U>& other): m_alignment(other.get_alignment()) {}
    template <typename U>
    operator AlignedAllocator<U>() const { return AlignedAllocator<U>(m_alignment); }
};
