#pragma once
#include <vector>
#include <cstring>
#include "./utility/span.h"

template <typename T>
class RingBuffer 
{
private:
    std::vector<T> m_data;
    size_t m_write_index = 0;
    size_t m_read_index = 0;
    size_t m_total_used = 0;
public:
    template <typename... Args>
    explicit RingBuffer(Args... args): m_data(std::forward<Args>(args)...) {}

    size_t get_size() const { return m_data.size(); }
    size_t get_total_used() const { return m_total_used; }
    size_t get_total_free() const { return get_size()-m_total_used; }
    bool is_full() const { return m_total_used == get_size(); }
    bool is_empty() const { return m_total_used == 0; }

    void write_from_src_with_overwrite(const tcb::span<const T> full_src) {
        auto src = full_src;
        if (src.size() > get_size()) {
            const size_t phantom_write_length = src.size() - get_size();
            m_write_index = (m_write_index + phantom_write_length) % get_size();
            src = src.last(get_size());
        }

        size_t write_length = src.size();
        size_t overflow_length = 0;
        const size_t end_index = m_write_index + write_length;
        if (end_index > get_size()) {
            overflow_length = end_index - get_size(); 
            write_length -= overflow_length;
        }

        std::memcpy(m_data.data() + m_write_index, src.data(), write_length * sizeof(T));
        std::memcpy(m_data.data(), src.data() + write_length, overflow_length * sizeof(T));
        m_write_index = (m_write_index + src.size()) % get_size();

        m_total_used += full_src.size();
        if (m_total_used > get_size()) {
            const size_t total_read_lost = m_total_used - get_size();
            m_read_index = (m_read_index + total_read_lost) % get_size();
            m_total_used = get_size();
        }
    }

    size_t write_from_src_until_full(tcb::span<const T> src) {
        const size_t total_free = get_total_free();
        const size_t write_length = (src.size() > total_free) ? total_free : src.size();
        write_from_src_with_overwrite(src.first(write_length));
        return write_length;
    }

    size_t read_to_dest(tcb::span<T> dest) {
        const size_t total_used = get_total_used();
        const size_t full_read_length = (dest.size() > total_used) ? total_used : dest.size();

        size_t read_length = full_read_length;
        size_t overflow_length = 0;
        const size_t end_index = m_read_index + read_length;
        if (end_index > get_size()) {
            overflow_length = end_index - get_size(); 
            read_length -= overflow_length;
        }

        std::memcpy(dest.data(), m_data.data() + m_read_index, read_length * sizeof(T));
        std::memcpy(dest.data() + read_length, m_data.data(), overflow_length * sizeof(T));
        m_read_index = (m_read_index + full_read_length) % get_size();
        m_total_used -= full_read_length;

        return full_read_length;
    }
};
