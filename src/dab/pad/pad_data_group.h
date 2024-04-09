#pragma once

#include <stddef.h>
#include <stdint.h>
#include <vector>
#include "utility/span.h"

// Append data group segments until we reach the required length
class PAD_Data_Group 
{
private:
    std::vector<uint8_t> m_buffer;
    size_t m_nb_required_bytes;
    size_t m_nb_curr_bytes;
public:
    PAD_Data_Group() {
        m_nb_required_bytes = 0;
        m_nb_curr_bytes = 0;
    }
    size_t Consume(tcb::span<const uint8_t> data);
    bool CheckCRC(void);
    void Reset(void);
    void SetRequiredBytes(const size_t N) { 
        m_buffer.resize(N);
        m_nb_required_bytes = N; 
    }
    size_t GetRequiredBytes(void) const { return m_nb_required_bytes; }
    size_t GetCurrentBytes(void) const { return m_nb_curr_bytes; }
    tcb::span<uint8_t> GetData(void) { return m_buffer; }
    bool IsComplete(void) const { return m_nb_curr_bytes == m_nb_required_bytes; }
};