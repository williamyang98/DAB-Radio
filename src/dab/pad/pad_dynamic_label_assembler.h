#pragma once
#include <stddef.h>
#include <stdint.h>
#include <vector>
#include "utility/span.h"

// Helper class to combine all of the dynamic label segments into the label
class PAD_Dynamic_Label_Assembler
{
private:
    struct Segment {
        size_t length;
    };
private:
    const size_t m_MAX_MESSAGE_BYTES = 128;
    const size_t m_MAX_SEGMENT_BYTES = 16;
    const size_t m_MAX_SEGMENTS = m_MAX_MESSAGE_BYTES/m_MAX_SEGMENT_BYTES;

    std::vector<Segment> m_segments;
    size_t m_nb_required_segments;

    // We have to recombine all of the variable sized segments into a coherent string
    std::vector<uint8_t> m_unordered_buf;
    std::vector<uint8_t> m_ordered_buf;
    uint8_t m_charset;
    size_t m_nb_ordered_bytes;
    bool m_is_changed;
public:
    PAD_Dynamic_Label_Assembler();
    void Reset(void);
    // Any segment which updates the completed label returns true
    // Any segment which doesn't update the completed label returns false
    bool UpdateSegment(tcb::span<const uint8_t> data, const size_t seg_num);
    void SetTotalSegments(const size_t total_segments);
    void SetCharSet(const uint8_t _charset);
    uint8_t GetCharSet(void) const { return m_charset; }
    tcb::span<uint8_t> GetData(void) { return m_ordered_buf; }
    size_t GetSize(void) const { return m_nb_ordered_bytes; }
    bool IsCompleted(void);
private:
    bool CombineSegments(void);
};
