#include "./pad_dynamic_label_assembler.h"
#include <stddef.h>
#include <stdint.h>
#include <fmt/format.h>
#include "utility/span.h"
#include "../dab_logging.h"
#define TAG "pad-dynamic-label"
static auto _logger = DAB_LOG_REGISTER(TAG);
#define LOG_MESSAGE(...) DAB_LOG_MESSAGE(TAG, fmt::format(__VA_ARGS__))
#define LOG_ERROR(...) DAB_LOG_ERROR(TAG, fmt::format(__VA_ARGS__))

PAD_Dynamic_Label_Assembler::PAD_Dynamic_Label_Assembler() {
    m_unordered_buf.resize(m_MAX_MESSAGE_BYTES);
    m_ordered_buf.resize(m_MAX_MESSAGE_BYTES);
    m_segments.resize(m_MAX_SEGMENTS);
    Reset();
}

void PAD_Dynamic_Label_Assembler::Reset(void) {
    m_charset = 0;
    m_nb_required_segments = 0;
    m_nb_ordered_bytes = 0;
    m_is_changed = true;
    for (size_t i = 0; i < m_MAX_SEGMENTS; i++) {
        m_segments[i].length = 0;
    }
}

bool PAD_Dynamic_Label_Assembler::UpdateSegment(tcb::span<const uint8_t> data, const size_t seg_num) {
    if (seg_num >= m_MAX_SEGMENTS) {
        LOG_ERROR("Segment index {} falls out of bounds [{},{}]", seg_num, 0, m_MAX_SEGMENT_BYTES-1);
        return false;
    }

    const size_t length = data.size();
    if ((length < 1) || (length > m_MAX_SEGMENT_BYTES)) {
        LOG_ERROR("Segment length {} falls out of bounds [{},{}]", length, 1, m_MAX_SEGMENT_BYTES);
        return false;
    }

    auto& segment = m_segments[seg_num];
    const auto ref_data = tcb::span(m_unordered_buf).subspan(seg_num * m_MAX_SEGMENT_BYTES, length);

    const bool length_mismatch = (segment.length != length);
    bool content_mismatch = false;
    for (size_t i = 0; i < length; i++) {
        if (ref_data[i] != data[i]) {
            content_mismatch = true;
            break;
        }
    }

    for (size_t i = 0; i < length; i++) {
        ref_data[i] = data[i];
    }

    // Received a conflicting length if a previous segment was provided
    if (length_mismatch && (segment.length != 0)) {
        LOG_ERROR("Segment {} has mismatching length {} != {}", seg_num, segment.length, length);
    }

    if (content_mismatch) {
        LOG_ERROR("Segment {} contents mismatch", seg_num);
    }

    segment.length = length;
    m_is_changed = m_is_changed || length_mismatch || content_mismatch;

    if (m_is_changed && CombineSegments()) {
        m_is_changed = false;
        return true;
    }

    return false;
}

void PAD_Dynamic_Label_Assembler::SetTotalSegments(const size_t total_segments) {
    if (m_nb_required_segments != total_segments) {
        m_is_changed = true;
    }
    m_nb_required_segments = total_segments;
}

void PAD_Dynamic_Label_Assembler::SetCharSet(const uint8_t _charset) {
    if (m_charset != _charset) {
        m_is_changed = true;
    }
    m_charset = _charset;
}

bool PAD_Dynamic_Label_Assembler::IsCompleted(void) {
    return (m_nb_ordered_bytes != 0);
}

bool PAD_Dynamic_Label_Assembler::CombineSegments(void) {
    if (m_nb_required_segments == 0) {
        return false;
    }

    for (size_t i = 0; i < m_nb_required_segments; i++) {
        const auto& segment = m_segments[i];
        if (segment.length == 0) {
            return false;
        }
    }

    // combine segments 
    size_t curr_byte = 0;
    for (size_t i = 0; i < m_nb_required_segments; i++) {
        const auto& segment = m_segments[i];
        const auto buf = tcb::span(m_unordered_buf).subspan(i * m_MAX_SEGMENT_BYTES, segment.length);
        for (size_t j = 0; j < segment.length; j++) {
            m_ordered_buf[curr_byte++] = buf[j];
        }
    }

    m_nb_ordered_bytes = curr_byte;
    return true;
}