#include "./MOT_assembler.h"
#include <stddef.h>
#include <stdint.h>
#include <optional>
#include <fmt/format.h>
#include "utility/span.h"
#include "../dab_logging.h"
#define TAG "mot-assembler"
static auto _logger = DAB_LOG_REGISTER(TAG);
#define LOG_MESSAGE(...) DAB_LOG_MESSAGE(TAG, fmt::format(__VA_ARGS__))
#define LOG_ERROR(...) DAB_LOG_ERROR(TAG, fmt::format(__VA_ARGS__))

MOT_Assembler::MOT_Assembler() {
    Reset();
}

void MOT_Assembler::Reset(void) {
    // We end up reusing this data
    for (auto& segment: m_segments) {
        segment.length = 0;
        segment.unordered_index = 0;
    }
    m_total_segments = std::nullopt;
    m_unordered_buffer.clear();
    m_ordered_buffer.clear();
    m_segments.clear();
}

void MOT_Assembler::SetTotalSegments(const size_t N) {
    m_total_segments = std::optional(N);
    m_segments.resize(N);
}

bool MOT_Assembler::AddSegment(const size_t index, tcb::span<const uint8_t> buf) {
    if (index >= m_segments.size()) {
        m_segments.resize(index+1);
    }

    if (m_total_segments.has_value() && (index >= m_total_segments.value())) {
        LOG_ERROR("Segment index overflow specified total segments ({}>={})", index, m_total_segments.value());
        return false;
    }

    auto& segment = m_segments[index];

    // Segment already present
    if (segment.length != 0) {
        if (segment.length != buf.size()) {
            LOG_ERROR("Segment {} has conflicting size {}!={}", index, segment.length, buf.size());
            return false;
        }
        // TODO: do we check if each segment has matching contents?
        return false;
    }

    // Add segment
    LOG_MESSAGE("Adding segment {} with length={}", index, buf.size());
    const size_t old_size = m_unordered_buffer.size();
    const size_t new_size = old_size + buf.size();
    segment.length = buf.size();
    segment.unordered_index = old_size;
    m_unordered_buffer.resize(new_size);

    auto dst_buf = tcb::span(m_unordered_buffer).subspan(old_size, buf.size());
    for (size_t i = 0; i < buf.size(); i++) {
        dst_buf[i] = buf[i];
    }
    const auto is_complete = CheckComplete();
    if (is_complete) {
        ReconstructOrderedBuffer();
    }
    return is_complete;
}

bool MOT_Assembler::CheckComplete(void) {
    // undefined segment length
    if (!m_total_segments.has_value()) {
        return false;
    }
 
    size_t total_size = 0;
    for (size_t i = 0; i < m_total_segments.value(); i++) {
        const auto& segment = m_segments[i];
        if (segment.length == 0) return false;
        total_size += segment.length;
    }
    if (total_size != m_unordered_buffer.size()) {
        return false;
    }
    return true;
}

void MOT_Assembler::ReconstructOrderedBuffer(void) {
    if (!m_total_segments.has_value()) return;

    LOG_MESSAGE("Reconstructing buffer with {} segments with length={}", m_total_segments.value(), m_unordered_buffer.size());
    m_ordered_buffer.resize(m_unordered_buffer.size());
    auto dst_buf = tcb::span(m_ordered_buffer);

    size_t curr_write_index = 0;
    for (size_t i = 0; i < m_total_segments.value(); i++) {
        auto& segment = m_segments[i];
        auto src_buf = tcb::span(m_unordered_buffer).subspan(segment.unordered_index, segment.length);
        for (size_t j = 0; j < segment.length; j++) {
            dst_buf[curr_write_index+j] = src_buf[j];
        }
        curr_write_index += segment.length;
    }
}