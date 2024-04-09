#include "./msc_data_packet_processor.h"
#include <stddef.h>
#include <stdint.h>
#include <memory>
#include <optional>
#include <fmt/format.h>
#include "utility/span.h"
#include "./msc_data_group_processor.h"
#include "../algorithms/crc.h"
#include "../dab_logging.h"
#include "../mot/MOT_processor.h"
#define TAG "msc-data-packet-processor"
static auto _logger = DAB_LOG_REGISTER(TAG);
#define LOG_MESSAGE(...) DAB_LOG_MESSAGE(TAG, fmt::format(__VA_ARGS__))
#define LOG_ERROR(...) DAB_LOG_ERROR(TAG, fmt::format(__VA_ARGS__))

// DOC: ETSI EN 300 401 
// Clause: 5.3.2 Packet mode - network level
// Clause: 5.3.3 Packet mode - data group level

// DOC: ETSI EN 300 401 
// Clause: 5.3.2.3 Packet CRC
static auto CRC16_CALC = []() {
    // Generator polynomial for the packet crc check
    // G(x) = x^16 + x^12 + x^5 + 1
    // initial = all 1s, complement = true
    const uint16_t au_crc_poly = 0b0001000000100001;
    auto calc = new CRC_Calculator<uint16_t>(au_crc_poly);
    calc->SetInitialValue(0xFFFF);
    calc->SetFinalXORValue(0xFFFF);
    return calc;
} ();

// Table 7: First/Last flags for packet mode
enum class PacketLocation: uint8_t {
    INTERMEDIATE = 0b00, 
    LAST = 0b01, 
    FIRST = 0b10, 
    SINGLE = 0b11,
};

// Table 6: Packet length
static const size_t PACKET_LENGTH[4] = { 24, 48, 72, 96 };

MSC_Data_Packet_Processor::MSC_Data_Packet_Processor() {
    m_assembly_buffer.reserve(128);
    m_mot_processor = std::make_unique<MOT_Processor>();
}

MSC_Data_Packet_Processor::~MSC_Data_Packet_Processor() = default;

size_t MSC_Data_Packet_Processor::ReadPacket(tcb::span<const uint8_t> buf) {
    constexpr size_t PACKET_HEADER_SIZE = 3;
    if (buf.size() < PACKET_HEADER_SIZE) {
        LOG_ERROR("Packet is too small to fit minimum non FEC header ({} < {})", buf.size(), PACKET_HEADER_SIZE);
        return buf.size();
    }
 
    // Figure 11: Packet structure
    const uint8_t packet_length_id   = (buf[0] & 0b11000000) >> 6;
    const uint8_t continuity_index   = (buf[0] & 0b00110000) >> 4;
    const uint8_t _packet_location   = (buf[0] & 0b00001100) >> 2;
    const uint16_t address  = (uint16_t(buf[0] & 0b00000011) << 8) |
                              (uint16_t(buf[1] & 0b11111111) << 0);
    // const uint8_t command_flag       = (buf[2] & 0b10000000) >> 7;
    const uint8_t useful_data_length = (buf[2] & 0b01111111) >> 0;

    const size_t packet_length = PACKET_LENGTH[packet_length_id];
    if (buf.size() < packet_length) {
        LOG_ERROR("Packet length smaller than minimum specified in headers ({} < {})", buf.size(), packet_length);
        return buf.size();
    }
    auto packet = buf.first(packet_length);

    constexpr size_t PACKET_CRC_SIZE = 2;
    const size_t data_field_length = packet.size()-PACKET_CRC_SIZE-PACKET_HEADER_SIZE;
    if (data_field_length < useful_data_length) {
        LOG_ERROR("Packet data field length ({}) is smaller than specified useful length in headers ({})", data_field_length, useful_data_length);
        return buf.size();
    }

    const auto crc_buf = packet.last(PACKET_CRC_SIZE);
    const auto crc_data = packet.first(PACKET_HEADER_SIZE + data_field_length);
    const uint16_t crc_rx = (crc_buf[0] << 8) | crc_buf[1];
    const uint16_t crc_pred = CRC16_CALC->Process(crc_data);
    const bool is_crc_valid = (crc_rx == crc_pred);
    if (!is_crc_valid) {
        LOG_MESSAGE("[crc16] is_match={} crc_pred={:04X} crc_rx={:04X}", is_crc_valid, crc_pred, crc_rx);
        return packet_length;
    }

    const auto data_field = packet.subspan(PACKET_HEADER_SIZE, useful_data_length);
    const auto packet_location = static_cast<PacketLocation>(_packet_location);
 
    // Determine if we should scratch current assembly
    const uint8_t expected_continuity_index = (m_last_continuity_index+1) % 4; // mod4 counter
    const bool is_continuity_assured = (expected_continuity_index == continuity_index);
    m_last_continuity_index = continuity_index;

    switch (packet_location) {
    case PacketLocation::SINGLE:
        HandleDataGroup(data_field);
        break;
    case PacketLocation::FIRST:
        ResetAssembler(); 
        m_last_address = std::optional(address);
        PushPiece(data_field);
        break;
    case PacketLocation::INTERMEDIATE:
        if (m_last_address != std::optional(address) || !is_continuity_assured) {
            ResetAssembler();
        } else {
            PushPiece(data_field);
        }
        break;
    case PacketLocation::LAST:
        if (m_last_address != std::optional(address) || !is_continuity_assured) {
            ResetAssembler();
        } else {
            PushPiece(data_field);
            HandleDataGroup(m_assembly_buffer);
            ResetAssembler();
        }
        break;
    }

    return packet_length;
}

void MSC_Data_Packet_Processor::PushPiece(tcb::span<const uint8_t> piece) {
    const size_t old_size = m_assembly_buffer.size();
    const size_t new_size = old_size + piece.size();
    m_assembly_buffer.resize(new_size);
    for (size_t i = 0; i < piece.size(); i++) {
        size_t j = i + old_size;
        m_assembly_buffer[j] = piece[i];
    }
    m_total_packets++;
}

void MSC_Data_Packet_Processor::ResetAssembler() {
    m_last_address = std::nullopt;
    m_total_packets = 0;
    m_assembly_buffer.resize(0);
}

void MSC_Data_Packet_Processor::HandleDataGroup(tcb::span<const uint8_t> data_group) {
    const auto res = MSC_Data_Group_Process(data_group);
    using Status = MSC_Data_Group_Process_Result::Status;
    if (res.status != Status::SUCCESS) {
        return;
    }

    // DOC: ETSI EN 300 401
    // Clause 5.3.3.1 - MSC data group header 
    // Depending on what the MSC data group is used for the header might have certain fields
    // For a MOT (multimedia object transfer) transported via XPAD we need the following:
    // 1. Segment number - So we can reassemble the MOT object
    if (!res.has_segment_field) {
        LOG_ERROR("Missing segment field in MSC XPAD header");
        return;
    }
    // 2. Transport id - So we can identify if a new MOT object is being transmitted
    if (!res.has_transport_id) {
        LOG_ERROR("Missing transport if field in MSC XPAD header");
        return;
    }

    MOT_MSC_Data_Group_Header header;
    header.data_group_type = static_cast<MOT_Data_Type>(res.data_group_type);
    header.continuity_index = res.continuity_index;
    header.repetition_index = res.repetition_index;
    header.is_last_segment = res.segment_field.is_last_segment;
    header.segment_number = res.segment_field.segment_number;
    header.transport_id = res.transport_id;
    m_mot_processor->Process_MSC_Data_Group(header, res.data_field);
}
