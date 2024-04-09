#pragma once

#include <stddef.h>
#include <stdint.h>
#include <functional>
#include <memory>
#include <optional>
#include <utility>
#include <vector>
#include "utility/span.h"

class Reed_Solomon_Decoder;

class MSC_Reed_Solomon_Data_Packet_Processor
{
public:
    // packet, is_corrected
    using Callback = std::function<void(tcb::span<const uint8_t>, bool)>;
private:
    std::vector<uint8_t> m_rs_encoded_buf;
    std::vector<int> m_rs_error_positions;
    std::vector<uint8_t> m_rs_data_table;
    std::vector<uint8_t> m_pop_buf;
    std::vector<uint8_t> m_ring_buf;
    size_t m_ring_read_head = 0;
    size_t m_ring_write_head = 0;
    size_t m_ring_size = 0;
    size_t m_ring_total_bytes_discarded = 0;
    size_t m_ring_total_packets_discarded = 0;
    std::optional<uint8_t> m_last_counter = std::nullopt;
    Callback m_callback = nullptr;
    std::unique_ptr<Reed_Solomon_Decoder> m_rs_decoder;
public:
    MSC_Reed_Solomon_Data_Packet_Processor();
    ~MSC_Reed_Solomon_Data_Packet_Processor();
    size_t ReadPacket(tcb::span<const uint8_t> buf);
    void SetCallback(const Callback& callback) { m_callback = callback; } 
    void SetCallback(Callback&& callback) { m_callback = std::move(callback); } 
private:
    void PushIntoRingBuffer(tcb::span<const uint8_t> buf, uint8_t packet_length_id);
    tcb::span<const uint8_t> PopRingBuffer();
    void ClearRingBuffer();
    void PerformReedSolomonCorrection();
    void ResetRingBuffer();
};
