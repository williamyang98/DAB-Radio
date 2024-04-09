#include "./msc_reed_solomon_data_packet_processor.h"
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <memory>
#include <optional>
#include <fmt/format.h>
#include "utility/span.h"
#include "../algorithms/reed_solomon_decoder.h"
#include "../dab_logging.h"
#define TAG "msc-reed-solomon-data-packet-processor"
static auto _logger = DAB_LOG_REGISTER(TAG);
#define LOG_MESSAGE(...) DAB_LOG_MESSAGE(TAG, fmt::format(__VA_ARGS__))
#define LOG_ERROR(...) DAB_LOG_ERROR(TAG, fmt::format(__VA_ARGS__))

// ETSI EN 300 401
// Clause: 5.3.5 FEC for MSC packet mode
// Table 6: Packet length
static const size_t PACKET_LENGTH[4] = { 24, 48, 72, 96 };
// Figure 15: Structure of FEC frame
static constexpr size_t RS_DATA_BYTES = 188;
static constexpr size_t RS_PARITY_BYTES = 16;
static constexpr size_t RS_MESSAGE_BYTES = RS_DATA_BYTES + RS_PARITY_BYTES;
static constexpr size_t RS_TOTAL_ROWS = 12;
// We pad the RS(204,188) code to RS(255,239) by adding zero symbols to the left of the message
static constexpr size_t RS_PADDING_BYTES = 255 - RS_MESSAGE_BYTES;
// Clause: 5.3.5.2 Transport of RS data
static constexpr size_t APPLICATION_DATA_TABLE_SIZE = 2256;
static_assert(RS_DATA_BYTES*RS_TOTAL_ROWS == APPLICATION_DATA_TABLE_SIZE);

static constexpr size_t RS_DATA_TABLE_SIZE = 192;
static constexpr size_t FEC_PACKET_LENGTH = 24;
static constexpr size_t TOTAL_FEC_PACKETS = 9;
static constexpr size_t FEC_PACKET_HEADER_SIZE = 2;
static constexpr size_t FEC_PACKET_DATA_FIELD_SIZE = FEC_PACKET_LENGTH-FEC_PACKET_HEADER_SIZE;
static constexpr size_t FEC_PACKET_PADDING_SIZE = 6;
static constexpr size_t TOTAL_RING_BUFFER_SIZE = APPLICATION_DATA_TABLE_SIZE + FEC_PACKET_LENGTH*TOTAL_FEC_PACKETS;
static_assert(RS_DATA_TABLE_SIZE == (FEC_PACKET_DATA_FIELD_SIZE*TOTAL_FEC_PACKETS - FEC_PACKET_PADDING_SIZE));

MSC_Reed_Solomon_Data_Packet_Processor::MSC_Reed_Solomon_Data_Packet_Processor() {
    m_rs_encoded_buf.resize(RS_MESSAGE_BYTES);
    m_rs_error_positions.resize(RS_PARITY_BYTES);
    m_rs_data_table.resize(RS_DATA_TABLE_SIZE);
    m_ring_buf.resize(TOTAL_RING_BUFFER_SIZE);
    // ETSI EN 300 401
    // Clause: 5.3.5.1 FEC frame
    // P(x) = x^8 + x^4 + x^3 + x^2 + 1
    constexpr int GALOIS_FIELD_POLY = 0b100011101;
    // G(x) = (x+λ^0)*(x+λ^1)*...*(x+λ^15)
    constexpr int CODE_TOTAL_ROOTS = 16; // same as number of rs parity bits
    // We pad the RS(204,188) code to RS(255,239) by adding zero symbols to the left of the message
    m_rs_decoder = std::make_unique<Reed_Solomon_Decoder>(8, GALOIS_FIELD_POLY, 0, 1, CODE_TOTAL_ROOTS, int(RS_PADDING_BYTES));
}

MSC_Reed_Solomon_Data_Packet_Processor::~MSC_Reed_Solomon_Data_Packet_Processor() = default;

size_t MSC_Reed_Solomon_Data_Packet_Processor::ReadPacket(tcb::span<const uint8_t> buf) {
    if (buf.size() < FEC_PACKET_HEADER_SIZE) {
        LOG_ERROR("Packet is too small to fit minimum FEC header ({} < {})", buf.size(), FEC_PACKET_HEADER_SIZE);
        return buf.size();
    }

    // Figure 11: Packet structure
    uint8_t packet_length_id         = (buf[0] & 0b11000000) >> 6;
    const uint8_t counter            = (buf[0] & 0b00111100) >> 2;
    const uint16_t address  = (uint16_t(buf[0] & 0b00000011) << 8) |
                              (uint16_t(buf[1] & 0b11111111) << 0);

    // Clause: 5.3.5.2 Transport of RS data
    constexpr uint16_t FEC_ADDRESS = 0b11'1111'1110;
    const bool is_fec_packet = (FEC_ADDRESS == address);
    if (is_fec_packet) {
        // Ignore provided packet length since that may be incorrect/corrupted
        packet_length_id = 0b00;
    }

    const size_t packet_length = PACKET_LENGTH[packet_length_id];
    if (buf.size() < packet_length) {
        LOG_ERROR("Packet length smaller than minimum specified in headers ({} < {})", buf.size(), packet_length);
        return buf.size();
    }

    auto packet = buf.first(packet_length);
    PushIntoRingBuffer(packet, packet_length_id);
    if (!is_fec_packet) {
        return packet_length;
    }
 
    const bool is_fec_invalid = [&]() -> bool {;
        // Clause 5.3.5.2 Transport of RS data
        // FEC packets count from 0 to 8 (inclusive)
        if (m_last_counter.has_value()) {
            const uint8_t expected_counter = m_last_counter.value() + 1;
            return expected_counter != counter;
        } else {
            return (counter != 0); 
        }
    } ();
 
    // Eject all packets and skip correction
    if (is_fec_invalid) {
        m_last_counter = std::nullopt;
        ClearRingBuffer();
        return packet_length;
    }

    m_last_counter = std::optional(counter);
    if (counter != uint8_t(TOTAL_FEC_PACKETS-1)) {
        return packet_length;
    }

    // All FEC packets are stored check if we can perform decoding
    if (m_ring_size != TOTAL_RING_BUFFER_SIZE) {
        ClearRingBuffer();
    } else {
        PerformReedSolomonCorrection();
    }
    m_last_counter = std::nullopt;
    ResetRingBuffer();
    return packet_length;
}

void MSC_Reed_Solomon_Data_Packet_Processor::ClearRingBuffer() {
    while (true) {
        auto stored_packet = PopRingBuffer();
        if (stored_packet.empty()) break;
        if (m_callback != nullptr) m_callback(stored_packet, false);
    }
    assert(m_ring_size == 0);
}

void MSC_Reed_Solomon_Data_Packet_Processor::PushIntoRingBuffer(tcb::span<const uint8_t> packet, uint8_t packet_length_id) {
    const size_t packet_length = PACKET_LENGTH[packet_length_id];
    assert(packet.size() == packet_length);
    // Free up last packet/s to make room for new packet
    while (true) {
        const size_t total_free = m_ring_buf.size() - m_ring_size;
        if (total_free >= packet_length) break;
        const uint8_t other_header = m_ring_buf[m_ring_read_head];
        const uint8_t other_packet_length_id = (other_header & 0b11000000) >> 6;
        const size_t other_packet_length = PACKET_LENGTH[other_packet_length_id];
        assert(m_ring_size >= other_packet_length);
        m_ring_size -= other_packet_length;
        m_ring_read_head = (m_ring_read_head+other_packet_length) % m_ring_buf.size();
        m_ring_total_bytes_discarded += other_packet_length;
        m_ring_total_packets_discarded++;
    }
 
    // override with correct packet length if we want to ignore the source which can be corrupted
    uint8_t header = packet[0];
    header = (header & 0b00111111) | ((packet_length_id & 0b11) << 6);
    m_ring_buf[m_ring_write_head] = header;
    for (size_t i = 1; i < packet_length; i++) {
        const size_t j = (m_ring_write_head+i) % m_ring_buf.size();
        m_ring_buf[j] = packet[i];
    }
    m_ring_size += packet_length;
    m_ring_write_head = (m_ring_write_head+packet_length) % m_ring_buf.size();
}

tcb::span<const uint8_t> MSC_Reed_Solomon_Data_Packet_Processor::PopRingBuffer() {
    if (m_ring_size == 0) return {};
    const uint8_t header = m_ring_buf[m_ring_read_head];
    const uint8_t packet_length_id = (header & 0b11000000) >> 6;
    const size_t packet_length = PACKET_LENGTH[packet_length_id];
    assert(m_ring_size >= packet_length);

    m_pop_buf.resize(packet_length);
    for (size_t i = 0; i < packet_length; i++) {
        const size_t j = (m_ring_read_head+i) % m_ring_buf.size();
        m_pop_buf[i] = m_ring_buf[j];
    }
    m_ring_size -= packet_length;
    m_ring_read_head = (m_ring_read_head+packet_length) % m_ring_buf.size();
    return m_pop_buf;
}

void MSC_Reed_Solomon_Data_Packet_Processor::ResetRingBuffer() {
    m_ring_read_head = 0;
    m_ring_write_head = 0;
    m_ring_size = 0;
    m_ring_total_bytes_discarded = 0;
    m_ring_total_packets_discarded = 0;
}

void MSC_Reed_Solomon_Data_Packet_Processor::PerformReedSolomonCorrection() {
    assert(m_ring_size == TOTAL_RING_BUFFER_SIZE);

    // Figure 17: Complete FEC packet set
    for (size_t i = 0; i < TOTAL_FEC_PACKETS; i++) {
        // Remove header from FEC packets
        const size_t i_ring = m_ring_read_head + APPLICATION_DATA_TABLE_SIZE + i*FEC_PACKET_LENGTH + FEC_PACKET_HEADER_SIZE;
        const size_t i_table = i*FEC_PACKET_DATA_FIELD_SIZE;
 
        size_t data_field_size = FEC_PACKET_DATA_FIELD_SIZE;
        // Figure 17: Complete FEC packet set
        // Last FEC packet has 6 padding bytes which we ignore
        if (i == (TOTAL_FEC_PACKETS-1)) {
            constexpr size_t TOTAL_PADDING_BYTES = 6;
            data_field_size = (FEC_PACKET_DATA_FIELD_SIZE-TOTAL_PADDING_BYTES);
        }
        for (size_t j = 0; j < data_field_size; j++) {
            const size_t j_ring = (i_ring+j) % m_ring_buf.size();
            m_rs_data_table[i_table+j] = m_ring_buf[j_ring];
        }
    }

    // Figure 15: Structure of FEC frame
    for (size_t y = 0; y < RS_TOTAL_ROWS; y++) {
        // Read application data table (transform from ring row-wise to table column-wise)
        for (size_t i = 0; i < RS_DATA_BYTES; i++) {
            const size_t x = i;
            const size_t i_offset = i*RS_TOTAL_ROWS + y;
            const size_t i_ring = (m_ring_read_head + i_offset) % m_ring_buf.size();
            m_rs_encoded_buf[x] = m_ring_buf[i_ring];
        }
        // Read rs data table (transform from ring row-wise to table column-wise)
        for (size_t i = 0; i < RS_PARITY_BYTES; i++) {
            const size_t x = RS_DATA_BYTES + i;
            const size_t i_offset = i*RS_TOTAL_ROWS + y;
            m_rs_encoded_buf[x] = m_rs_data_table[i_offset];
        }

        const int error_count = m_rs_decoder->Decode(m_rs_encoded_buf.data(), m_rs_error_positions.data(), 0);
        LOG_MESSAGE("[reed-solomon] row={}/{} error_count={}", y, RS_TOTAL_ROWS, error_count);
        // rs decoder returns -1 to indicate too many errors
        if (error_count < 0) {
            LOG_ERROR("[reed-solomon] Too many errors to correct");
            continue;
        }
        // correct any errors
        for (int i = 0; i < error_count; i++) {
            // NOTE: Phil Karn's reed solmon decoder returns the position of errors 
            // with the amount of padding added onto it
            const int x_err = m_rs_error_positions[i] - int(RS_PADDING_BYTES);
            if (x_err < 0) {
                LOG_ERROR("[reed-solomon] Got a negative error index={}, row={}/{}", x_err, y, RS_TOTAL_ROWS);
                continue;
            }
            // correct data packets
            if (x_err < int(RS_DATA_BYTES)) {
                const size_t i_offset = size_t(x_err)*RS_TOTAL_ROWS + y;
                const size_t i_ring = (m_ring_read_head + i_offset) % m_ring_buf.size();
                m_ring_buf[i_ring] = m_rs_encoded_buf[size_t(x_err)]; 
            }
            // dont correct fec packets they aren't used for anything else
        }
    }

    size_t total_read = 0;
    while (total_read < APPLICATION_DATA_TABLE_SIZE) {
        auto buf = PopRingBuffer();
        if (buf.empty()) break;
        if (m_callback != nullptr) m_callback(buf, true);
        total_read += buf.size();
    }
    assert(total_read == APPLICATION_DATA_TABLE_SIZE);
}
