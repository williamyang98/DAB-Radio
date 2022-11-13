#include "pad_dynamic_label_assembler.h"

#include "easylogging++.h"
#include "fmt/core.h"

#define LOG_MESSAGE(...) CLOG(INFO, "pad-dynamic-label") << fmt::format(__VA_ARGS__)
#define LOG_ERROR(...) CLOG(ERROR, "pad-dynamic-label") << fmt::format(__VA_ARGS__)

PAD_Dynamic_Label_Assembler::PAD_Dynamic_Label_Assembler() {
    unordered_buf = new uint8_t[MAX_MESSAGE_BYTES];
    ordered_buf = new uint8_t[MAX_MESSAGE_BYTES];
    segments = new Segment[MAX_SEGMENTS];
    Reset();
}

PAD_Dynamic_Label_Assembler::~PAD_Dynamic_Label_Assembler() {
    delete [] unordered_buf;
    delete [] ordered_buf;
    delete [] segments;
}


void PAD_Dynamic_Label_Assembler::Reset(void) {
    charset = 0;
    nb_required_segments = 0;
    nb_ordered_bytes = 0;
    is_changed = true;
    for (int i = 0; i < MAX_SEGMENTS; i++) {
        segments[i].length = 0;
    }
}

bool PAD_Dynamic_Label_Assembler::UpdateSegment(const uint8_t* data, const int length, const int seg_num) {
    if ((seg_num < 0) || (seg_num >= MAX_SEGMENTS)) {
        LOG_ERROR("Segment index {} falls out of bounds [{},{}]", seg_num, 0, MAX_SEGMENT_BYTES-1);
        return false;
    }

    if ((length <= 0) || (length > MAX_SEGMENT_BYTES)) {
        LOG_ERROR("Segment length {} falls out of bounds [{},{}]", length, 1, MAX_SEGMENT_BYTES);
        return false;
    }

    auto& segment = segments[seg_num];
    const int index = seg_num * MAX_SEGMENT_BYTES;
    auto* ref_data = &unordered_buf[index];

    const bool length_mismatch = (segment.length != length);
    bool content_mismatch = false;

    for (int i = 0; i < length; i++) {
        ref_data[i] = data[i];
        content_mismatch = content_mismatch || (ref_data[i] != data[i]);
    }

    // Received a conflicting length if a previous segment was provided
    if (length_mismatch && (segment.length != 0)) {
        LOG_ERROR("Segment {} has mismatching length {} != {}", seg_num, segment.length, length);
    }

    if (content_mismatch) {
        LOG_ERROR("Segment {} contents mismatch", seg_num);
    }

    segment.length = length;
    is_changed = is_changed || length_mismatch || content_mismatch;

    if (is_changed && CombineSegments()) {
        is_changed = false;
        return true;
    }

    return false;
}

void PAD_Dynamic_Label_Assembler::SetTotalSegments(const int total_segments) {
    if (nb_required_segments != total_segments) {
        is_changed = true;
    }
    nb_required_segments = total_segments;
}

void PAD_Dynamic_Label_Assembler::SetCharSet(const uint8_t _charset) {
    if (charset != _charset) {
        is_changed = true;
    }
    charset = _charset;
}

bool PAD_Dynamic_Label_Assembler::IsCompleted(void) {
    return (nb_ordered_bytes != 0);
}

bool PAD_Dynamic_Label_Assembler::CombineSegments(void) {
    if (nb_required_segments == 0) {
        return false;
    }

    for (int i = 0; i < nb_required_segments; i++) {
        auto& segment = segments[i];
        if (segment.length == 0) {
            return false;
        }
    }

    // combine segments 
    int curr_byte = 0;
    for (int i = 0; i < nb_required_segments; i++) {
        auto& segment = segments[i];
        auto* buf = &unordered_buf[i * MAX_SEGMENT_BYTES];
        for (int j = 0; j < segment.length; j++) {
            ordered_buf[curr_byte++] = buf[j];
        }
    }

    nb_ordered_bytes = curr_byte;
    return true;
}