#include "./pad_dynamic_label.h"
#include <stddef.h>
#include <stdint.h>
#include <algorithm>
#include <memory>
#include <string_view>
#include <fmt/format.h>
#include "utility/span.h"
#include "./pad_dynamic_label_assembler.h"
#include "../dab_logging.h"
#define TAG "pad-dynamic-label"
static auto _logger = DAB_LOG_REGISTER(TAG);
#define LOG_MESSAGE(...) DAB_LOG_MESSAGE(TAG, fmt::format(__VA_ARGS__))
#define LOG_ERROR(...) DAB_LOG_ERROR(TAG, fmt::format(__VA_ARGS__))

#undef min // NOLINT

constexpr size_t TOTAL_CRC16_BYTES = 2;
constexpr size_t TOTAL_HEADER_BYTES = 2;
constexpr size_t MIN_DATA_GROUP_BYTES = TOTAL_CRC16_BYTES + TOTAL_HEADER_BYTES;

// DOC: ETSI EN 300 401
// Clause 7.4.5.2 - Dynamic label 
// The following code refers heavily to the specified clause

PAD_Dynamic_Label::PAD_Dynamic_Label() {
    m_data_group.SetRequiredBytes(MIN_DATA_GROUP_BYTES);
    m_state = State::WAIT_START;
    m_group_type = GroupType::LABEL_SEGMENT;
    m_assembler = std::make_unique<PAD_Dynamic_Label_Assembler>();
    m_previous_toggle_flag = 0;
}

PAD_Dynamic_Label::~PAD_Dynamic_Label() = default;

void PAD_Dynamic_Label::ProcessXPAD(const bool is_start, tcb::span<const uint8_t> buf) {
    const size_t N = buf.size();
    size_t curr_byte = 0;
    bool curr_is_start = is_start;
    while (curr_byte < N) {
        const size_t nb_remain = N-curr_byte;
        const size_t nb_read = ConsumeBuffer(curr_is_start, {&buf[curr_byte], nb_remain});
        curr_byte += nb_read;
        curr_is_start = false;
    }
}

size_t PAD_Dynamic_Label::ConsumeBuffer(const bool is_start, tcb::span<const uint8_t> buf) {
    const size_t N = buf.size();
    if ((m_state == State::WAIT_START) && !is_start) {
        return N;
    }

    if (is_start) {
        if ((m_state != State::WAIT_START) && !m_data_group.IsComplete()) {
            LOG_MESSAGE("Discarding partial data group {}/{}", m_data_group.GetCurrentBytes(), m_data_group.GetRequiredBytes());
        }
        m_data_group.Reset();
        m_data_group.SetRequiredBytes(MIN_DATA_GROUP_BYTES);
        m_state = State::READ_LENGTH;
    }

    size_t nb_read_bytes = 0;

    // Don't read past the header field since we need to calculate the length from it
    if (m_state == State::READ_LENGTH) {
        const size_t nb_remain_header_bytes = TOTAL_HEADER_BYTES-m_data_group.GetCurrentBytes();
        if (nb_remain_header_bytes > 0) {
            const size_t nb_remain = N-nb_read_bytes;
            const size_t M = std::min(nb_remain_header_bytes, nb_remain);
            nb_read_bytes += m_data_group.Consume({&buf[nb_read_bytes], M});
        }

        if (m_data_group.GetCurrentBytes() >= TOTAL_HEADER_BYTES) {
            ReadGroupHeader();
            m_state = State::READ_DATA;
        }
    }

    if (m_state != State::READ_DATA) {
        return nb_read_bytes;
    }

    // Assemble the data group
    const size_t nb_remain = N-nb_read_bytes;
    nb_read_bytes += m_data_group.Consume({&buf[nb_read_bytes], nb_remain});
    LOG_MESSAGE("Progress partial data group {}/{}", m_data_group.GetCurrentBytes(), m_data_group.GetRequiredBytes());

    if (!m_data_group.IsComplete()) {
        return nb_read_bytes;
    }

    if (!m_data_group.CheckCRC()) {
        LOG_ERROR("CRC mismatch on data group");
        m_state = State::WAIT_START;
        m_data_group.Reset();
        m_data_group.SetRequiredBytes(MIN_DATA_GROUP_BYTES);
        return nb_read_bytes;
    }

    // We have a valid data group, read it
    switch (m_group_type) {
    case GroupType::LABEL_SEGMENT:
        InterpretLabelSegment();
        break;
    case GroupType::COMMAND:
        InterpretCommand();
        break;
    default:
        LOG_ERROR("Unknown data group type {}", static_cast<int>(m_group_type));
        break;
    }

    m_state = State::WAIT_START;
    m_data_group.Reset();
    m_data_group.SetRequiredBytes(MIN_DATA_GROUP_BYTES);
    return nb_read_bytes;
}

void PAD_Dynamic_Label::ReadGroupHeader(void) {
    const auto buf = m_data_group.GetData();

    const uint8_t toggle_flag     = (buf[0] & 0b10000000) >> 7;
    // const uint8_t first_last_flag = (buf[0] & 0b01100000) >> 5;
    const uint8_t control_flag    = (buf[0] & 0b00010000) >> 4;

    // Control segment has no data field
    if (control_flag) {
        m_data_group.SetRequiredBytes(TOTAL_HEADER_BYTES + TOTAL_CRC16_BYTES);
        m_group_type = GroupType::COMMAND;
        // TODO: manage toggle flag for command data group
    // Label segment has specified length
    } else {
        const uint8_t length = (buf[0] & 0b00001111) >> 0;
        const int nb_data_group_bytes = TOTAL_HEADER_BYTES + TOTAL_CRC16_BYTES + length + 1;
        m_data_group.SetRequiredBytes(nb_data_group_bytes);    
        m_group_type = GroupType::LABEL_SEGMENT;

        // If we have a different dynamic label
        if (toggle_flag != m_previous_toggle_flag) {
            m_previous_toggle_flag = toggle_flag;
            m_assembler->Reset();
        }
    } 
}

void PAD_Dynamic_Label::InterpretLabelSegment(void) {
    const auto buf = m_data_group.GetData();
    const size_t N = m_data_group.GetRequiredBytes();

    // const uint8_t toggle_flag     = (buf[0] & 0b10000000) >> 7;
    const uint8_t first_last_flag = (buf[0] & 0b01100000) >> 5;
    // const uint8_t control_flag    = (buf[0] & 0b00010000) >> 4;
    // const uint8_t length          = (buf[0] & 0b00001111) >> 0;
    const uint8_t field2          = (buf[1] & 0b11110000) >> 4;
    // const uint8_t rfa0            = (buf[1] & 0b00001111) >> 0;

    const bool is_first = (first_last_flag & 0b10) != 0;
    const bool is_last  = (first_last_flag & 0b01) != 0;

    uint8_t seg_num = 0;
    if (!is_first) {
        // const uint8_t rfa1 = (field2 & 0b1000) >> 3;
        seg_num            = (field2 & 0b0111) >> 0;
    }
    if (is_last) {
        m_assembler->SetTotalSegments(seg_num+1);
    }

    if (is_first) {
        const uint8_t charset = field2;
        m_assembler->SetCharSet(charset);
    } 

    const auto* data = &buf[TOTAL_HEADER_BYTES];
    const size_t nb_data_bytes = N-TOTAL_HEADER_BYTES-TOTAL_CRC16_BYTES;
    const bool is_changed = m_assembler->UpdateSegment({data, nb_data_bytes}, seg_num);
    if (!is_changed) {
        return;
    }

    const auto label = m_assembler->GetData();
    const size_t nb_label_bytes = m_assembler->GetSize();
    const auto label_str = std::string_view(
        reinterpret_cast<const char*>(label.data()), 
        nb_label_bytes);

    LOG_MESSAGE("label[{}]={}", nb_label_bytes, label_str);
    m_obs_on_label_change.Notify(label_str, m_assembler->GetCharSet());
}

void PAD_Dynamic_Label::InterpretCommand(void) {
    const auto buf = m_data_group.GetData();

    const uint8_t command = (buf[0] & 0b00001111) >> 0;
    // const uint8_t field2  = (buf[1] & 0b11110000) >> 4;
    // const uint8_t field3  = (buf[1] & 0b00001111) >> 0;

    // DOC: ETSI EN 300 401
    // Clause 7.4.5.2 - Dynamic label 
    switch (command) {
    // Clear display command
    case 0b0000:
        LOG_MESSAGE("command=clear_display");
        m_obs_on_command.Notify((uint8_t)Command::CLEAR);
        break;
    // TODO: Dynamic label plus command, see ETSI TS 102 980
    case 0b1000:
        LOG_MESSAGE("command=dynamic_label_plus");
        break;
    // Reserved for future use
    default:
        LOG_ERROR("Command code {} reserved for future use", command);
        break;
    }
}