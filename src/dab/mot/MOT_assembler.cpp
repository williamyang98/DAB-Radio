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
    total_segments = 0;
    curr_unordered_index = 0;

    for (auto& segment: segments) {
        segment.length = 0;
        segment.unordered_index = 0;
    }
}

void MOT_Assembler::SetTotalSegments(const int N) {
    total_segments = N;
    segments.resize(total_segments);
}

bool MOT_Assembler::AddSegment(const int index, const uint8_t* buf, const int N) {
    if (index >= segments.size()) {
        segments.resize(index+1);
    }

    if ((total_segments != 0) && (index >= total_segments)) {
        LOG_ERROR("Total segments given as {} but got segment {}", total_segments, index);
        return false;
    }

    auto& segment = segments[index];

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
    segment.unordered_index = curr_unordered_index;
    unordered_buffer.resize(curr_unordered_index+N);

    auto* all_buf = unordered_buffer.data();
    auto* dst_buf = &all_buf[curr_unordered_index];
    for (int i = 0; i < N; i++) {
        dst_buf[i] = buf[i];
    }
    curr_unordered_index += N;

    const auto is_complete = CheckComplete();
    if (is_complete) {
        ReconstructOrderedBuffer();
    }
    return is_complete;
}

bool MOT_Assembler::CheckComplete(void) {
    // undefined segment length
    if (total_segments == 0) {
        return false;
    }

    for (int i = 0; i < total_segments; i++) {
        auto& segment = segments[i];
        if (segment.length == 0) {
            return false;
        }
    }

    return true;
}

void MOT_Assembler::ReconstructOrderedBuffer(void) {
    LOG_MESSAGE("Reconstructing buffer with {} segments with length={}",
        total_segments, curr_unordered_index);

    auto* all_src_buf = unordered_buffer.data();

    ordered_buffer.resize(curr_unordered_index);
    auto* dst_buf = ordered_buffer.data();
    int curr_ordered_index = 0;
    for (int i = 0; i < total_segments; i++) {
        auto& segment = segments[i];
        const auto* src_buf = &all_src_buf[segment.unordered_index];
        for (int j = 0; j < segment.length; j++) {
            dst_buf[curr_ordered_index++] = src_buf[j];
        }
    }
}