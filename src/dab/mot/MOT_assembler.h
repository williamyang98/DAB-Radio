#pragma once

#include <stdint.h>
#include <stddef.h>
#include <vector>
#include "utility/span.h"

// Assembles MOT entity from segments
class MOT_Assembler 
{
private:
    struct Segment {
        size_t unordered_index;
        size_t length;
        Segment() {
            unordered_index = 0;
            length = 0;
        }
    };
private:
    std::vector<uint8_t> unordered_buffer;
    std::vector<uint8_t> ordered_buffer;
    std::vector<Segment> segments;
    size_t total_segments = 0;
    size_t curr_unordered_index = 0;
public:
    MOT_Assembler();
    ~MOT_Assembler() {}
    void Reset(void);
    void SetTotalSegments(const size_t N);
    bool AddSegment(const size_t index, const uint8_t* buf, const size_t N);
    tcb::span<uint8_t> GetData() { return ordered_buffer; }
    bool CheckComplete();
private:
    void ReconstructOrderedBuffer();
};