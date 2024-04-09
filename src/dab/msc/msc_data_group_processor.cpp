#include "./msc_data_group_processor.h"
#include <stddef.h>
#include <stdint.h>
#include <fmt/format.h>
#include "utility/span.h"
#include "../algorithms/crc.h"
#include "../dab_logging.h"
#define TAG "msc-data-group-processor"
#define LOG_MESSAGE(...) DAB_LOG_MESSAGE(TAG, fmt::format(__VA_ARGS__))
#define LOG_ERROR(...) DAB_LOG_ERROR(TAG, fmt::format(__VA_ARGS__))

static auto Generate_CRC_Calc() {
    // DOC: ETSI EN 300 401
    // Clause 5.3.3.4 - MSC data group CRC
    // CRC16 Polynomial is given by:
    // G(x) = x^16 + x^12 + x^5 + 1
    // POLY = 0b 0001 0000 0010 0001 = 0x1021
    static const uint16_t crc16_poly = 0x1021;
    static auto crc16_calc = new CRC_Calculator<uint16_t>(crc16_poly);
    crc16_calc->SetInitialValue(0xFFFF);    // initial value all 1s
    crc16_calc->SetFinalXORValue(0xFFFF);   // transmitted crc is 1s complemented

    return crc16_calc;
};

static auto CRC16_CALC = Generate_CRC_Calc();

MSC_Data_Group_Process_Result MSC_Data_Group_Process(tcb::span<const uint8_t> data_group) {
    using Status = MSC_Data_Group_Process_Result::Status;
    MSC_Data_Group_Process_Result res;

    // 5.3.3.1 MSC data group header
    auto buf = data_group;
    constexpr size_t MIN_HEADER_SIZE = 2;
    if (buf.size() < MIN_HEADER_SIZE) {
        LOG_ERROR("Data group smaller than minimum header size ({} < {})", buf.size(), MIN_HEADER_SIZE);
        res.status = Status::SHORT_GROUP_HEADER;
        return res;
    }

    const uint8_t extension_flag   = (buf[0] & 0b10000000) >> 7;
    const uint8_t crc_flag         = (buf[0] & 0b01000000) >> 6;
    const uint8_t segment_flag     = (buf[0] & 0b00100000) >> 5;
    const uint8_t user_access_flag = (buf[0] & 0b00010000) >> 4;
    const uint8_t data_group_type  = (buf[0] & 0b00001111) >> 0;
    const uint8_t continuity_index = (buf[1] & 0b11110000) >> 4;
    const uint8_t repetition_index = (buf[1] & 0b00001111) >> 0;
    buf = buf.subspan(MIN_HEADER_SIZE);
    res.has_header_fields = true;
    res.data_group_type = data_group_type;
    res.continuity_index = continuity_index;
    res.repetition_index = repetition_index;

    // Clause: 5.3.3.4 MSC data group CRC
    if (crc_flag) {
        constexpr size_t CRC_SIZE = 2;
        if (buf.size() < CRC_SIZE) {
            LOG_ERROR("[msc-data-group] Insufficient size for crc16 ({} < {})", buf.size(), CRC_SIZE);
            res.status = Status::SHORT_CRC_FIELD;
            return res;
        }
        buf = buf.first(buf.size() - CRC_SIZE);

        const auto crc_data = data_group.first(data_group.size() - CRC_SIZE);
        const auto crc_buf = data_group.last(CRC_SIZE);
        const uint16_t crc_rx = (crc_buf[0] << 8) | crc_buf[1];
        const uint16_t crc_calc = CRC16_CALC->Process(crc_data);
        const bool is_crc_valid = (crc_rx == crc_calc);
        res.has_crc = true;
        res.crc_rx = crc_rx;
        res.crc_calc = crc_calc;
        if (!is_crc_valid) {
            LOG_ERROR("[msc-data-group] is_match={} crc_rx={:04X} crc_calc={:04X}", is_crc_valid, crc_rx, crc_calc);
            res.status = Status::CRC_INVALID;
            return res;
        }
    }

    if (extension_flag) {
        constexpr size_t EXTENSION_FIELD_SIZE = 2;
        if (buf.size() < EXTENSION_FIELD_SIZE) {
            LOG_ERROR("Data group too short to store extension field ({} < {})", buf.size(), EXTENSION_FIELD_SIZE); 
            res.status = Status::SHORT_EXTENSION_FIELD;
            return res;
        }
        const uint16_t extension_field = (buf[0] << 8) | buf[1];
        buf = buf.subspan(EXTENSION_FIELD_SIZE);
        // ETSI TS 102 367: Conditional access
        res.has_extension_field = true;
        res.extension_field = extension_field;
    }
 
    // Clause: 5.3.3.2 Session header
    if (segment_flag) {
        constexpr size_t SEGMENT_SIZE = 2; 
        if (buf.size() < SEGMENT_SIZE) {
            LOG_ERROR("Data group too short to store segment field ({} < {})", buf.size(), SEGMENT_SIZE); 
            res.status = Status::SHORT_SEGMENT_FIELD;
            return res;
        }
        const uint8_t is_last         =  (buf[0] & 0b10000000) >> 7;
        const uint16_t segment_number = ((buf[0] & 0b01111111) << 8) | buf[1];
        buf = buf.subspan(SEGMENT_SIZE);
        res.has_segment_field = true;
        res.segment_field.is_last_segment = (is_last == 0b1);
        res.segment_field.segment_number = segment_number;
    }

    // Clause: 5.3.3.2 Session header
    if (user_access_flag) {
        constexpr size_t ACCESS_FIELD_HEADER_SIZE = 1;
        if (buf.size() < ACCESS_FIELD_HEADER_SIZE) {
            LOG_ERROR("Data group too short to store access field header ({} < {})", buf.size(), ACCESS_FIELD_HEADER_SIZE); 
            res.status = Status::SHORT_ACCESS_FIELD_HEADER;
            return res;
        }
        // const uint8_t rfa = (buf[0] & 0b11100000) >> 5;
        const uint8_t transport_id_flag = (buf[0] & 0b00010000) >> 4;
        const uint8_t length_indicator = (buf[0] & 0b00001111) >> 0;
        buf = buf.subspan(ACCESS_FIELD_HEADER_SIZE);

        if (length_indicator > buf.size()) {
            LOG_ERROR("Data group too short to store user access fields ({} < {})", buf.size(), length_indicator); 
            res.status = Status::SHORT_ACCESS_FIELDS;
            return res;
        }
        auto fields = buf.first(length_indicator);
        buf = buf.subspan(length_indicator);

        if (transport_id_flag) {
            constexpr size_t TRANSPORT_ID_SIZE = 2;
            if (fields.size() < TRANSPORT_ID_SIZE) {
                LOG_ERROR("User access fields too short to store transport id ({} < {})", fields.size(), TRANSPORT_ID_SIZE); 
                res.status = Status::SHORT_TRANSPORT_ID_FIELD;
                return res;
            }
            const uint16_t transport_id = (fields[0] << 8) | fields[1];
            res.has_transport_id = true;
            res.transport_id = transport_id;
            fields = fields.subspan(TRANSPORT_ID_SIZE);
        }

        res.has_user_access_fields = true;
        res.user_access_fields = fields;
    }

    // Clause: 5.3.3.3 MSC data group data field
    constexpr size_t MAX_DATA_FIELD_SIZE = 8191;
    if (buf.size() >= MAX_DATA_FIELD_SIZE) {
        LOG_ERROR("Data field exceeds maximum allowed size in standard ({} > {})", buf.size(), MAX_DATA_FIELD_SIZE);
        res.status = Status::OVERFLOW_MAX_DATA_FIELD_SIZE;
        return res;
    }

    res.status = Status::SUCCESS;
    res.data_field = buf;
    return res;
}