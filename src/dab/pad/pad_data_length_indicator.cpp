#include "./pad_data_length_indicator.h"
#include <stddef.h>
#include <stdint.h>
#include <fmt/format.h>
#include "utility/span.h"
#include "../dab_logging.h"
#define TAG "pad-data-length"
static auto _logger = DAB_LOG_REGISTER(TAG);
#define LOG_MESSAGE(...) DAB_LOG_MESSAGE(TAG, fmt::format(__VA_ARGS__))
#define LOG_ERROR(...) DAB_LOG_ERROR(TAG, fmt::format(__VA_ARGS__))

constexpr size_t TOTAL_DATA_GROUP_BYTES = 4;

PAD_Data_Length_Indicator::PAD_Data_Length_Indicator() {
    m_data_group.Reset();
    m_data_group.SetRequiredBytes(TOTAL_DATA_GROUP_BYTES);

    m_is_length_available = false;
    m_length = 0;
}

void PAD_Data_Length_Indicator::ResetLength(void) { 
    m_is_length_available = false;
    m_length = 0;
    m_data_group.Reset();
    m_data_group.SetRequiredBytes(TOTAL_DATA_GROUP_BYTES);
}

void PAD_Data_Length_Indicator::ProcessXPAD(tcb::span<const uint8_t> buf) {
    const size_t N = buf.size();
    size_t curr_byte = 0;
    while (curr_byte < N) {
        const size_t nb_remain = N-curr_byte;
        const size_t nb_read = Consume({&buf[curr_byte], nb_remain});
        curr_byte += nb_read;
    }
}

size_t PAD_Data_Length_Indicator::Consume(tcb::span<const uint8_t> buf) {
    const size_t nb_read = m_data_group.Consume(buf);
    LOG_MESSAGE("Progress partial data group {}/{}", m_data_group.GetCurrentBytes(), m_data_group.GetRequiredBytes());

    if (!m_data_group.IsComplete()) {
        return nb_read;
    }

    if (!m_data_group.CheckCRC()) {
        LOG_ERROR("CRC mismatch on data group");
        m_data_group.Reset();
        m_data_group.SetRequiredBytes(TOTAL_DATA_GROUP_BYTES);
        return nb_read;
    }

    Interpret();
    m_data_group.Reset();
    m_data_group.SetRequiredBytes(TOTAL_DATA_GROUP_BYTES);
    return nb_read;
}

void PAD_Data_Length_Indicator::Interpret(void) {
    // DOC: ETSI EN 300 401
    // Clause 7.4.5.1.1: X-PAD data group for data group length indicator 
    // Figure 34: Structure of the X-PAD data group for the data group length indicator 
    auto buf = m_data_group.GetData();

    const uint8_t rfa      =  (buf[0] & 0b11000000) >> 6;
    const uint16_t _length = ((buf[0] & 0b00111111) << 8) |
                             ((buf[1] & 0b11111111) >> 0);
    
    m_length = _length;
    m_is_length_available = true;
    LOG_MESSAGE("length={} rfa={}", m_length, rfa);
}