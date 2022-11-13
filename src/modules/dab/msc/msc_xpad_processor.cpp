#include "msc_xpad_processor.h"
#include "algorithms/crc.h"

#include "easylogging++.h"
#include "fmt/core.h"

#define LOG_MESSAGE(...) CLOG(INFO, "msc-xpad-processor") << fmt::format(__VA_ARGS__)
#define LOG_ERROR(...) CLOG(ERROR, "msc-xpad-processor") << fmt::format(__VA_ARGS__)

static const auto Generate_CRC_Calc() {
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

MSC_XPAD_Processor::ProcessResult MSC_XPAD_Processor::Process(const uint8_t* buf, const int N) {
    // DOC: ETSI EN 300 401
    // Clause 5.3.3 - Packet mode - Data group level
    // Figure 12 - Structure of MSC data group
    ProcessResult res;
    res.is_success = false;

    int curr_byte = 0;
    int nb_remain = N-curr_byte;
    const uint8_t* data = NULL;

    // Part 1: (required) Data group header
    const int MIN_DATA_GROUP_HEADER_BYTES = 2;
    if (nb_remain < MIN_DATA_GROUP_HEADER_BYTES) {
        LOG_ERROR("Insufficient length for min data group header {}<{}", 
            nb_remain, MIN_DATA_GROUP_HEADER_BYTES);
        return res;
    }

    data = &buf[curr_byte];
    curr_byte += MIN_DATA_GROUP_HEADER_BYTES;
    nb_remain = N-curr_byte;
    const uint8_t extension_flag   = (data[0] & 0b10000000) >> 7;
    const uint8_t crc_flag         = (data[0] & 0b01000000) >> 6;
    const uint8_t segment_flag     = (data[0] & 0b00100000) >> 5;
    const uint8_t user_access_flag = (data[0] & 0b00010000) >> 4;
    const uint8_t data_group_type  = (data[0] & 0b00001111) >> 0;
    const uint8_t continuity_index = (data[1] & 0b11110000) >> 4;
    const uint8_t repetition_index = (data[1] & 0b00001111) >> 0;

    res.data_group_type = data_group_type;    
    res.continuity_index = continuity_index;
    res.repetition_index = repetition_index;

    // Part 1.1: (optional) Extension field is used to carry CA information
    const int TOTAL_EXTENSION_FIELD_BYTES = 2;
    if (extension_flag) {
        if (nb_remain < TOTAL_EXTENSION_FIELD_BYTES) {
            LOG_ERROR("Insufficient length for extended data group header {}<{}", 
                nb_remain, TOTAL_EXTENSION_FIELD_BYTES);
            return res;
        }

        data = &buf[curr_byte];
        curr_byte += TOTAL_EXTENSION_FIELD_BYTES;
        nb_remain = N-curr_byte;
        uint16_t extension_field = (data[0] << 8) | data[1];

        // DOC: ETSI TS 102 367
        // This field is used for conditional access 
        res.has_extension_field = true;
        res.extension_field = extension_field;
    }

    // Part 2: Session header
    // Part 2.1: (optional) Segment field
    const int MIN_SEGMENT_FIELD_BYTES = 2;
    if (segment_flag) {
        if (nb_remain < MIN_SEGMENT_FIELD_BYTES) {
            LOG_ERROR("Insufficient length for min session header {}<{}", 
                nb_remain, MIN_SEGMENT_FIELD_BYTES);
            return res;
        }

        data = &buf[curr_byte];
        curr_byte += MIN_SEGMENT_FIELD_BYTES;
        nb_remain = N-curr_byte;
        const uint8_t last_flag       =  (data[0] & 0b10000000) >> 7;
        const uint16_t segment_number = ((data[0] & 0b01111111) << 8) |
                                        ((data[1] & 0b11111111) >> 0);
        res.has_segment_field = true;
        res.segment_field.is_last_segment = last_flag;
        res.segment_field.segment_number = segment_number;
    }

    // Part 2.2: (optional) User access field 
    const int MIN_USER_ACCESS_FIELD_BYTES = 1;
    if (user_access_flag) {
        if (nb_remain < MIN_USER_ACCESS_FIELD_BYTES) {
            LOG_ERROR("Insufficient length for min user access field {}<{}",
                nb_remain, MIN_USER_ACCESS_FIELD_BYTES);
            return res;
        }

        data = &buf[curr_byte];
        curr_byte += MIN_USER_ACCESS_FIELD_BYTES;
        nb_remain = N-curr_byte;
        const uint8_t rfa0              = (data[0] & 0b11100000) >> 5;
        const uint8_t transport_id_flag = (data[0] & 0b00010000) >> 4;
        const uint8_t length_indicator  = (data[0] & 0b00001111) >> 0;

        res.has_user_access_field = true;

        // Part 2.2.1: (optional) Transport id field
        const int TOTAL_TRANSPORT_ID_BYTES = transport_id_flag ? 2 : 0;
        if (nb_remain < TOTAL_TRANSPORT_ID_BYTES) {
            LOG_ERROR("Insufficient length for transport id {}<{}",
                nb_remain, TOTAL_TRANSPORT_ID_BYTES);
            return res;
        }

        if (transport_id_flag) {
            data = &buf[curr_byte];
            curr_byte += TOTAL_TRANSPORT_ID_BYTES;
            nb_remain = N-curr_byte;
            const uint16_t transport_id = (data[0] << 8) | data[1];
            
            res.user_access_field.has_transport_id = true;
            res.user_access_field.transport_id = transport_id;
        }
        
        // Part 2.2.2: (required) End user address field
        const int nb_end_user_address_bytes = static_cast<int>(length_indicator)-TOTAL_TRANSPORT_ID_BYTES;
        if (nb_remain < nb_end_user_address_bytes) {
            LOG_ERROR("Insufficient length for end user address field by indicated length {}<{}",
                nb_remain, nb_end_user_address_bytes);
            return res;
        }

        auto* end_user_address = &buf[curr_byte];
        curr_byte += nb_end_user_address_bytes;
        nb_remain = N-curr_byte;

        res.user_access_field.end_address = end_user_address;
        res.user_access_field.nb_end_address_bytes = nb_end_user_address_bytes;
    }

    // Part 3: (required) Data group data field
    auto* data_field = &buf[curr_byte];
    const int TOTAL_CRC16_BYTES = 2;
    const int nb_data_bytes = crc_flag ? (nb_remain-TOTAL_CRC16_BYTES) : nb_remain;
    if (nb_data_bytes < 0) {
        LOG_ERROR("Insufficient length for data field where CRC?={} {}<{}",
            crc_flag, nb_data_bytes, 0);
        return res;
    }

    // Part 3.1: (optional) CRC16 on entire buffer
    if (crc_flag) {
        const uint16_t crc16_rx = (buf[N-2] << 8) | buf[N-1];
        const uint16_t crc16_calc = CRC16_CALC->Process(buf, N-TOTAL_CRC16_BYTES);
        const bool is_valid = (crc16_rx == crc16_calc);
        if (!is_valid) {
            LOG_ERROR("CRC mismatch {:04X}!={:04X}", crc16_rx, crc16_calc);
            return res;
        }
    }

    res.data_field = data_field;
    res.nb_data_field_bytes = nb_data_bytes;
    res.is_success = true;

    LOG_MESSAGE("type={} cont={:>2} rep={} "
                "ext?={} ext={:>2} "
                "seg?={} last={} segnum={} " 
                "tid?={} tid={:>4} "
                "user_access?={} nb_end_addr={} crc?={} nb_data={:>4d}",
                 res.data_group_type, res.continuity_index, res.repetition_index,
                 res.has_extension_field, res.extension_field, 
                 res.has_segment_field, res.segment_field.is_last_segment, res.segment_field.segment_number,
                 res.user_access_field.has_transport_id, res.user_access_field.transport_id,
                 res.has_user_access_field, res.user_access_field.nb_end_address_bytes, 
                 crc_flag, res.nb_data_field_bytes);

    return res;
}