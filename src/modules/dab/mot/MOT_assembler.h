#pragma once

#include <stdint.h>
#include <vector>
#include "utility/span.h"

// Assembles MOT entity from segments
class MOT_Assembler 
{
private:
    struct Segment {
        int unordered_index;
        int length;
        Segment() {
            unordered_index = 0;
            length = 0;
        }
    };
private:
    std::vector<uint8_t> unordered_buffer;
    std::vector<uint8_t> ordered_buffer;
    std::vector<Segment> segments;
    int total_segments;
    int curr_unordered_index;
public:
    MOT_Assembler();
    ~MOT_Assembler() {}
    void Reset(void);
    void SetTotalSegments(const int N);
    bool AddSegment(const int index, const uint8_t* buf, const int N);
    tcb::span<uint8_t> GetData() { return ordered_buffer; }
    bool CheckComplete();
private:
    void ReconstructOrderedBuffer();
};