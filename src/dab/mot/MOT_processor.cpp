#include "MOT_processor.h"
#include "algorithms/modified_julian_date.h"

#include "easylogging++.h"
#include "fmt/core.h"

#define LOG_MESSAGE(...) CLOG(INFO, "mot-processor") << fmt::format(##__VA_ARGS__)
#define LOG_ERROR(...) CLOG(ERROR, "mot-processor") << fmt::format(##__VA_ARGS__)
#define LOG_WARN(...) CLOG(WARNING, "mot-processor") << fmt::format(##__VA_ARGS__)

constexpr static MOT_Data_Type VALID_DATA_TYPES[] = {
    ECM_EMM_DATA, HEADER, 
    UNSCRAMBLED_BODY, SCRAMBLED_BODY, 
    UNCOMPRESSED_DIRECTORY, COMPRESSED_DIRECTORY
};
constexpr static int TOTAL_VALID_DATA_TYPES = sizeof(VALID_DATA_TYPES) / sizeof(MOT_Data_Type);

static bool ValidateDataType(const MOT_Data_Type type) {
    for (int i = 0; i < TOTAL_VALID_DATA_TYPES; i++) {
        if (VALID_DATA_TYPES[i] == type) {
            return true;
        }
    }
    return false;
}

void MOT_Processor::Process_Segment(const MOT_MSC_Data_Group_Header header, const uint8_t* buf, const int N) {
    const int MIN_SEGMENT_HEADER_BYTES = 2;
    if (N < MIN_SEGMENT_HEADER_BYTES) {
        LOG_ERROR("Insufficient length for segment header {}<{}", N, MIN_SEGMENT_HEADER_BYTES);
        return;
    }

    const uint8_t repetition_count =  (buf[0] & 0b11100000) >> 5;
    const uint16_t segment_size    = ((buf[0] & 0b00011111) << 8) | buf[1];

    auto* data = &buf[MIN_SEGMENT_HEADER_BYTES];
    const int nb_data_bytes = N-MIN_SEGMENT_HEADER_BYTES;

    if (nb_data_bytes != segment_size) {
        LOG_ERROR("Segment length mismatch seg_size={} data_size={}", segment_size, nb_data_bytes);
        return;
    }

    if (!ValidateDataType(header.data_group_type)) {
        LOG_ERROR("Got invalid data group type in MSC header {}", header.data_group_type);
        return;
    }

    if (header.repetition_index != repetition_count) {
        LOG_WARN("Mismatching repetition count in MSC header and segmentation header {}!={}", 
            header.repetition_index, repetition_count);
    }

    // TODO: For MOT body entities the time taken to assemble them can be quite long
    //       Signal the progress of the assembler to a listener for MOT body entities
    auto& assembler_table = GetAssemblerTable(header.transport_id);
    auto& assembler = GetAssembler(assembler_table, header.data_group_type);
    if (header.is_last_segment) {
        assembler.SetTotalSegments(header.segment_number+1);
    }
    const bool is_updated = assembler.AddSegment(header.segment_number, data, nb_data_bytes);
    if (is_updated) {
        CheckEntityComplete(header.transport_id);
    }
}

MOT_Assembler_Table& MOT_Processor::GetAssemblerTable(const mot_transport_id_t transport_id) {
    auto res = assembler_tables.find(transport_id);
    if (res == assembler_tables.end()) {
        LOG_MESSAGE("Got new transport_id={}", transport_id);
        res = assembler_tables.insert({transport_id, {}}).first;
    }
    return res->second;
}

MOT_Assembler& MOT_Processor::GetAssembler(MOT_Assembler_Table& table, const MOT_Data_Type type) {
    auto res = table.find(type);
    if (res == table.end()) {
        res = table.insert({type, {}}).first;
    }
    return res->second;
}

bool MOT_Processor::CheckEntityComplete(const mot_transport_id_t transport_id) {
    // TODO: Support directory mode MOT entities
    auto& assembler_table = GetAssemblerTable(transport_id);
    auto& header_assembler = GetAssembler(assembler_table, MOT_Data_Type::HEADER);
    auto& body_assembler = GetAssembler(assembler_table, MOT_Data_Type::UNSCRAMBLED_BODY);

    if (!header_assembler.CheckComplete()) {
        return false;
    }

    if (!body_assembler.CheckComplete()) {
        return false;
    }

    const auto* header_buf = header_assembler.GetData();
    const auto* body_buf = body_assembler.GetData();
    const int N_header = header_assembler.GetLength();
    const int N_body = body_assembler.GetLength();

    MOT_Entity entity;
    entity.transport_id = transport_id;
    entity.body_buf = body_buf;
    entity.nb_body_bytes = N_body;

    // TODO: Sometimes we get the header first, meaning we can extra useful length information
    //       Signal this information to a listener
    const bool is_success = ProcessHeader(&entity.header, header_buf, N_header);
    if (!is_success) {
        return false;
    }
    
    if (entity.header.header_size != N_header) {
        LOG_ERROR("Mismatching header length fields {}!={}",
            entity.header.header_size, N_header);
        return false;
    }

    if (entity.header.body_size != N_body) {
        LOG_ERROR("Mismatching body length fields {}!={}",
            entity.header.body_size, N_body);
        return false;
    }

    LOG_MESSAGE("Completed a MOT header entity with header={} body={} tid={}",
        entity.header.header_size, entity.header.body_size,
        entity.transport_id);
    obs_on_entity_complete.Notify(entity);
    return true;
}

bool MOT_Processor::ProcessHeader(MOT_Header_Entity* entity, const uint8_t* buf, const int N) {
    // DOC: ETSI EN 301 234
    // Clause 5.3.1: Single object transmission (MOT header mode) 
    // Figure 14: Repeated transmission of header information
    // The header consists of the header core and header extension

    int curr_byte = 0;
    const uint8_t* data = &buf[curr_byte];
    int nb_remain = N-curr_byte;

    // DOC: ETSI EN 301 234
    // Clause 6.1: Header core 
    const int TOTAL_HEADER_CORE = 7;
    if (nb_remain < TOTAL_HEADER_CORE) {
        LOG_ERROR("Insufficient length for header core {}<{}", nb_remain, TOTAL_HEADER_CORE);
        return false;
    }

    data = &buf[curr_byte];
    curr_byte += TOTAL_HEADER_CORE;
    nb_remain = N-curr_byte;
    const uint32_t body_size        = ((data[0] & 0b11111111) << 20) | 
                                      ((data[1] & 0b11111111) << 12) | 
                                      ((data[2] & 0b11111111) << 4)  | 
                                      ((data[3] & 0b11110000) >> 4);
    const uint16_t header_size      = ((data[3] & 0b00001111) << 9) |
                                      ((data[4] & 0b11111111) << 1) |
                                      ((data[5] & 0b10000000) >> 7);
    const uint8_t content_type      = ((data[5] & 0b01111110) >> 1);
    const uint16_t content_sub_type = ((data[5] & 0b00000001) << 8) |
                                      ((data[6] & 0b11111111) >> 0);
    
    entity->body_size = body_size;
    entity->header_size = header_size;
    entity->content_type = content_type;
    entity->content_sub_type = content_sub_type;

    // DOC: ETSI TS 101 756
    // Clause 6: Registered tables in ETSI EN 301 234 (MOT) 
    // Table 17: Content type and content subtypes 
    LOG_MESSAGE("[header-core] body_size={} header_size={} content_type={}|{}", 
        body_size, header_size, content_type, content_sub_type);

    // DOC: ETSI EN 301 234
    // Clause 6.2: Header extension 
    while (curr_byte < N) {
        data = &buf[curr_byte];
        curr_byte += 1;
        nb_remain = N-curr_byte;

        // Parameter length indicator
        const uint8_t pli      = (data[0] & 0b11000000) >> 6;
        const uint8_t param_id = (data[0] & 0b00111111) >> 0;

        uint32_t nb_data_bytes = 0;
        bool is_length_indicator = false;

        switch (pli) {
        // No data field
        case 0b00:  
            nb_data_bytes = 0;
            is_length_indicator = false;
            break; 
        // 1byte data field
        case 0b01:
            nb_data_bytes = 1;
            is_length_indicator = false;
            break;
        // 4byte data field
        case 0b10:
            nb_data_bytes = 4;
            is_length_indicator = false;
            break;
        // Depends on data field indicator
        case 0b11:
            nb_data_bytes = 0;
            is_length_indicator = true;
            break;
        }

        if (is_length_indicator) {
            data = &buf[curr_byte];

            if (nb_remain < 1) {
                LOG_ERROR("Insufficient length for data field indicator {}<{}",
                    nb_remain, 1);
                return false;
            }

            const uint8_t ext_flag = (data[0] & 0b10000000) >> 7;
            if (ext_flag && (nb_remain < 2)) {
                LOG_ERROR("Insufficient length for extended data field indicator {}<{}",
                    nb_remain, 2);
                return false;
            }

            if (!ext_flag) {
                nb_data_bytes = ((data[0] & 0b01111111) << 0);
                curr_byte += 1;
            } else {
                nb_data_bytes = ((data[0] & 0b01111111) << 8) |
                                ((data[1] & 0b11111111) << 0);
                curr_byte += 2;
            }
            nb_remain = N-curr_byte;
        }

        if (nb_remain < static_cast<int>(nb_data_bytes)) {
            LOG_ERROR("Insufficient length for data field {}<{} pli={} param_id={}", 
                nb_remain, nb_data_bytes, pli, param_id);
            return false;
        }

        data = &buf[curr_byte];
        curr_byte += nb_data_bytes;
        nb_remain = N-curr_byte;

        ProcessHeaderExtensionParameter(entity, param_id, data, nb_data_bytes);
    }

    return true;
}

bool MOT_Processor::ProcessHeaderExtensionParameter(
    MOT_Header_Entity* entity, const uint8_t id, 
    const uint8_t* buf, const int N) 
{
    // DOC: ETSI EN 301 234
    // Clause 6.3: List of all MOT parameters in the MOT header extension 
    // Table 2: Coding of extension parameter 

    // User specific application parameters
    if ((id >= 0b100101) && (id <= 0b111111)) {
        MOT_Header_Extension_Parameter param;
        param.type = id;
        param.data = buf;
        param.nb_data_bytes = N;
        entity->user_app_params.push_back(param);
        return true;
    }

    if (id > 0b111111) {
        LOG_ERROR("[header-ext] Out of table param_id={} length={}", id, N);
        return false;
    }

    #define UNIMPLEMENTED_CASE(_id, name) {\
    case _id:\
        LOG_WARN("[header-ext] Unimplemented param_id={} length={} type=" name, _id, N);\
        return false;\
    }

    switch (id) {
    case 0b001100: return ProcessHeaderExtensionParameter_ContentName(entity, buf, N);
    case 0b000100: return ProcessHeaderExtensionParameter_ExpireTime(entity, buf, N);
    case 0b000101: return ProcessHeaderExtensionParameter_TriggerTime(entity, buf, N);
    UNIMPLEMENTED_CASE(0b000001, "permit_outdated_versions");
    UNIMPLEMENTED_CASE(0b000111, "retransmission_distance");
    UNIMPLEMENTED_CASE(0b001001, "expiration");
    UNIMPLEMENTED_CASE(0b001010, "priority");
    UNIMPLEMENTED_CASE(0b001011, "label");
    UNIMPLEMENTED_CASE(0b001101, "unique_body_version");
    UNIMPLEMENTED_CASE(0b010000, "mime_type");
    UNIMPLEMENTED_CASE(0b010001, "compression_type");
    UNIMPLEMENTED_CASE(0b100000, "additional_header");
    UNIMPLEMENTED_CASE(0b100001, "profile_subset");
    UNIMPLEMENTED_CASE(0b100011, "conditional_access_info");
    UNIMPLEMENTED_CASE(0b100100, "conditional_access_replacement_object");
    // Reserved for MOT protocol extension
    default:
        LOG_WARN("[header-ext] Reserved for extension: param_id={} length={}", id, N);
        return false;
    }
    #undef UNIMPLEMENTED_CASE
}

bool MOT_Processor::ProcessHeaderExtensionParameter_ContentName(MOT_Header_Entity* entity, const uint8_t* buf, const int N) {
    // DOC: ETSI EN 301 234
    // Clause 6.2.2.1.1: Content name
    if (N < 2) {
        LOG_ERROR("[header-ext] type=content_name Insufficient length for content name header and data {}<{}",
            N, 2);
        return false;
    }

    const uint8_t charset = (buf[0] & 0b11110000) >> 4;
    const uint8_t rfa0    = (buf[0] & 0b00001111) >> 0;

    const auto* name = &buf[1];
    const auto* name_str = reinterpret_cast<const char*>(name);
    const int nb_name_bytes = N-1;

    entity->content_name.exists = true;
    entity->content_name.charset = charset;
    entity->content_name.name = name_str;
    entity->content_name.nb_bytes = nb_name_bytes;

    LOG_MESSAGE("[header-ext] type=content_name charset={} rfa0={} name[{}]={}",
        charset, rfa0, nb_name_bytes, std::string_view(name_str, nb_name_bytes));
    return true;
}

bool MOT_Processor::ProcessHeaderExtensionParameter_ExpireTime(MOT_Header_Entity* entity, const uint8_t* buf, const int N) {
    // NOTE: The expire time field is defined by the following doc
    // DOC: ETSI TS 101 499
    // Clause 6.2.1: General
    // Table 3: MOT Parameters
    // For some reason it is not defined by the expected document
    // DOC: ETSI EN 301 234 
    return ProcessHeaderExtensionParameter_UTCTime(&(entity->expire_time), buf, N);
}

bool MOT_Processor::ProcessHeaderExtensionParameter_TriggerTime(MOT_Header_Entity* entity, const uint8_t* buf, const int N) {
    return ProcessHeaderExtensionParameter_UTCTime(&(entity->trigger_time), buf, N);
}

bool MOT_Processor::ProcessHeaderExtensionParameter_UTCTime(MOT_UTC_Time* entity, const uint8_t* buf, const int N) {
    // DOC: ETSI EN 301 234
    // Clause 6.2.4.1: Coding of time parameters 
    if (N < 4) {
        LOG_ERROR("[header-ext] type=utc_time Insufficient length for time header and data {}<{}", 
            N, 4);
        return false;
    }

    const uint8_t validity_flag =  (buf[0] & 0b10000000) >> 7;

    // The entire field is zeroed and trigger time means "now"
    if (!validity_flag) {
        entity->exists = true;
        entity->year = 0;
        entity->month = 0;
        entity->day = 0;
        entity->hours = 0;
        entity->minutes = 0;
        entity->seconds = 0;
        entity->milliseconds = 0;
        LOG_MESSAGE("[header-ext] type=utc_time valid={} datetime=NOW", validity_flag);
        return true;            
    }

    const uint32_t MJD_date = ((buf[0] & 0b01111111) << 10) |
                              ((buf[1] & 0b11111111) << 2)  |
                              ((buf[2] & 0b11000000) >> 6);
    const uint8_t rfu0      =  (buf[2] & 0b00110000) >> 4;
    const uint8_t UTC_flag  =  (buf[2] & 0b00001000) >> 4;
    const uint8_t hours     = ((buf[2] & 0b00000111) << 2) |
                              ((buf[3] & 0b11000000) >> 6);
    const uint8_t minutes   =  (buf[3] & 0b00111111) >> 0;
    uint8_t seconds = 0;
    uint16_t milliseconds = 0;

    if (UTC_flag) {
        if (N < 6) {
            LOG_ERROR("[header-ext] type=utc_time Insufficient length for time header and long UTC {}<{}", 
                N, 6);
            return false;
        }

        seconds      =  (buf[4] & 0b11111100) >> 2;
        milliseconds = ((buf[4] & 0b00000011) << 8) |
                       ((buf[5] & 0b11111111) >> 0);
    }

    int year, month, day;
    mjd_to_ymd(static_cast<long>(MJD_date), &year, &month, &day);

    entity->exists = true;
    entity->year = year;
    entity->month = month;
    entity->day = day;
    entity->hours = hours;
    entity->minutes = minutes;
    entity->seconds = seconds;
    entity->milliseconds = milliseconds;

    LOG_MESSAGE("[header-ext] type=utc_time valid={} utc={} date={:02}/{:02}/{:04} time={:02}:{:02}:{:02}.{:03}",
        validity_flag, UTC_flag, day, month, year, hours, minutes, seconds, milliseconds);
    return true;
}