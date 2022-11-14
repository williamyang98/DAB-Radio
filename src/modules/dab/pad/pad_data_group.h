#pragma once

#include <stdint.h>
#include <vector>
#include "utility/span.h"

// Append data group segments until we reach the required length
class PAD_Data_Group 
{
private:
    std::vector<uint8_t> buffer;
    size_t nb_required_bytes;
    size_t nb_curr_bytes;
public:
    PAD_Data_Group() {
        nb_required_bytes = 0;
        nb_curr_bytes = 0;
    }
    size_t Consume(tcb::span<const uint8_t> data);
    bool CheckCRC(void);
    void Reset(void);
    void SetRequiredBytes(const size_t N) { 
        buffer.resize(N);
        nb_required_bytes = N; 
    }
    size_t GetRequiredBytes(void) const { return nb_required_bytes; }
    size_t GetCurrentBytes(void) const { return nb_curr_bytes; }
    tcb::span<uint8_t> GetData(void) { return buffer; }
    bool IsComplete(void) const { return nb_curr_bytes == nb_required_bytes; }
};