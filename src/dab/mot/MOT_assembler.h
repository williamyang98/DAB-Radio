#pragma once

#include <stdint.h>
#include <stddef.h>
#include <vector>
#include <optional>
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
    std::vector<uint8_t> m_unordered_buffer;
    std::vector<uint8_t> m_ordered_buffer;
    std::vector<Segment> m_segments;
    std::optional<size_t> m_total_segments = std::nullopt;
public:
    MOT_Assembler();
    void Reset(void);
    void SetTotalSegments(const size_t N);
    bool AddSegment(const size_t index, tcb::span<const uint8_t> buf);
    tcb::span<uint8_t> GetData() { return m_ordered_buffer; }
    bool CheckComplete();
private:
    void ReconstructOrderedBuffer();
};