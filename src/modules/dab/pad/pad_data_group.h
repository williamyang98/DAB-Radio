#pragma once

#include <stdint.h>
#include <vector>

// Append data group segments until we reach the required length
class PAD_Data_Group 
{
private:
    std::vector<uint8_t> buffer;
    int nb_required_bytes;
    int nb_curr_bytes;
public:
    PAD_Data_Group() {
        nb_required_bytes = 0;
        nb_curr_bytes = 0;
    }
    int Consume(const uint8_t* data, const int N);
    bool CheckCRC(void);
    void Reset(void);
    void SetRequiredBytes(const int N) { 
        buffer.resize(N);
        nb_required_bytes = N; 
    }
    int GetRequiredBytes(void) const { return nb_required_bytes; }
    int GetCurrentBytes(void) const { return nb_curr_bytes; }
    uint8_t* GetData(void) { return buffer.data(); }
    bool IsComplete(void) const { return nb_curr_bytes == nb_required_bytes; }
};