#pragma once
#include <stdint.h>

// Helper class to combine all of the dynamic label segments into the label
class PAD_Dynamic_Label_Assembler
{
private:
    struct Segment {
        int length;
    };
private:
    const int MAX_MESSAGE_BYTES = 128;
    const int MAX_SEGMENT_BYTES = 16;
    const int MAX_SEGMENTS = MAX_MESSAGE_BYTES/MAX_SEGMENT_BYTES;

    Segment* segments;
    int nb_required_segments;

    // We have to recombine all of the variable sized segments into a coherent string
    uint8_t* unordered_buf;
    uint8_t* ordered_buf;
    uint8_t charset;
    int nb_ordered_bytes;
    bool is_changed;
public:
    PAD_Dynamic_Label_Assembler();
    ~PAD_Dynamic_Label_Assembler();
    void Reset(void);
    // Any segment which updates the completed label returns true
    // Any segment which doesn't update the completed label returns false
    bool UpdateSegment(const uint8_t* data, const int length, const int seg_num);
    void SetTotalSegments(const int total_segments);
    void SetCharSet(const uint8_t _charset);
    uint8_t GetCharSet(void) const { return charset; }
    uint8_t* GetData(void) { return ordered_buf; }
    int GetSize(void) const { return nb_ordered_bytes; }
    bool IsCompleted(void);
private:
    bool CombineSegments(void);
};
