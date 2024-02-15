#include "./MOT_assembler.h"
#include <fmt/core.h>

#include "../dab_logging.h"
#define TAG "mot-assembler"
static auto _logger = DAB_LOG_REGISTER(TAG);
#define LOG_MESSAGE(...) DAB_LOG_MESSAGE(TAG, fmt::format(__VA_ARGS__))
#define LOG_ERROR(...) DAB_LOG_ERROR(TAG, fmt::format(__VA_ARGS__))

MOT_Assembler::MOT_Assembler() {
    Reset();
}

void MOT_Assembler::Reset(void) {
    m_total_segments = 0;
    m_curr_unordered_index = 0;

    for (auto& segment: m_segments) {
        segment.length = 0;
        segment.unordered_index = 0;
    }
}

void MOT_Assembler::SetTotalSegments(const size_t N) {
    m_total_segments = N;
    m_segments.resize(m_total_segments);
}

bool MOT_Assembler::AddSegment(const size_t index, const uint8_t* buf, const size_t N) {
    if (index >= m_segments.size()) {
        m_segments.resize(index+1);
    }

    if ((m_total_segments != 0) && (index >= m_total_segments)) {
        LOG_ERROR("Total segments given as {} but got segment {}", m_total_segments, index);
        return false;
    }

    auto& segment = m_segments[index];

    // Segment already present
    if (segment.length != 0) {
        if (segment.length != N) {
            LOG_ERROR("Segment {} has conflicting size {}!={}", index, segment.length, N);
            return false;
        }
        // TODO: do we check if each segment has matching contents?
        return false;
    }

    // Add segment
    LOG_MESSAGE("Adding segment {} with length={}", index, N);
    segment.length = N;
    segment.unordered_index = m_curr_unordered_index;
    m_unordered_buffer.resize(m_curr_unordered_index+N);

    auto* all_buf = m_unordered_buffer.data();
    auto* dst_buf = &all_buf[m_curr_unordered_index];
    for (size_t i = 0; i < N; i++) {
        dst_buf[i] = buf[i];
    }
    m_curr_unordered_index += N;

    const auto is_complete = CheckComplete();
    if (is_complete) {
        ReconstructOrderedBuffer();
    }
    return is_complete;
}

bool MOT_Assembler::CheckComplete(void) {
    // undefined segment length
    if (m_total_segments == 0) {
        return false;
    }

    for (size_t i = 0; i < m_total_segments; i++) {
        auto& segment = m_segments[i];
        if (segment.length == 0) {
            return false;
        }
    }

    return true;
}

void MOT_Assembler::ReconstructOrderedBuffer(void) {
    LOG_MESSAGE("Reconstructing buffer with {} segments with length={}", m_total_segments, m_curr_unordered_index);

    auto* all_src_buf = m_unordered_buffer.data();

    m_ordered_buffer.resize(m_curr_unordered_index);
    auto* dst_buf = m_ordered_buffer.data();
    size_t curr_ordered_index = 0;
    for (size_t i = 0; i < m_total_segments; i++) {
        auto& segment = m_segments[i];
        const auto* src_buf = &all_src_buf[segment.unordered_index];
        for (size_t j = 0; j < segment.length; j++) {
            dst_buf[curr_ordered_index++] = src_buf[j];
        }
    }
}