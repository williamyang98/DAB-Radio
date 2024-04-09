#include "./MOT_processor.h"
#include <assert.h>
#include <stdint.h>
#include <cstring>
#include <optional>
#include <string_view>
#include <utility>
#include <fmt/format.h>
#include "utility/span.h"
#include "./MOT_assembler.h"
#include "./MOT_entities.h"
#include "../algorithms/modified_julian_date.h"
#include "../dab_logging.h"
#define TAG "mot-processor"
static auto _logger = DAB_LOG_REGISTER(TAG);
#define LOG_MESSAGE(...) DAB_LOG_MESSAGE(TAG, fmt::format(__VA_ARGS__))
#define LOG_WARN(...) DAB_LOG_WARN(TAG, fmt::format(__VA_ARGS__))
#define LOG_ERROR(...) DAB_LOG_ERROR(TAG, fmt::format(__VA_ARGS__))

constexpr static MOT_Data_Type VALID_DATA_TYPES[] = {
    MOT_Data_Type::ECM_EMM_DATA, MOT_Data_Type::HEADER, 
    MOT_Data_Type::UNSCRAMBLED_BODY, MOT_Data_Type::SCRAMBLED_BODY, 
    MOT_Data_Type::UNCOMPRESSED_DIRECTORY, MOT_Data_Type::COMPRESSED_DIRECTORY
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

MOT_Processor::MOT_Processor(const size_t max_transport_entities, const size_t max_header_entities) {
    m_assembler_tables.set_max_size(max_transport_entities);
    m_body_headers.set_max_size(max_header_entities);
}

void MOT_Processor::Process_MSC_Data_Group(const MOT_MSC_Data_Group_Header header, tcb::span<const uint8_t> buf) {
    // DOC: ETSI EN 301 234
    // Clause 5.1.1: Segmentation header 
    // Figure 7: Segmentation header
    const size_t MIN_SEGMENT_HEADER_BYTES = 2;
    if (buf.size() < MIN_SEGMENT_HEADER_BYTES) {
        LOG_ERROR("Insufficient length for segment header ({}<{})", buf.size(), MIN_SEGMENT_HEADER_BYTES);
        return;
    }

    const uint8_t repetition_count =  (buf[0] & 0b11100000) >> 5;
    const uint16_t segment_size    = ((buf[0] & 0b00011111) << 8) | buf[1];
 
    auto data = buf.subspan(MIN_SEGMENT_HEADER_BYTES);

    if (data.size() != segment_size) {
        LOG_ERROR("Segment length mismatch seg_size={} data_size={}", segment_size, data.size());
        return;
    }

    if (!ValidateDataType(header.data_group_type)) {
        LOG_ERROR("Got invalid data group type in MSC header {}", static_cast<uint8_t>(header.data_group_type));
        return;
    }

    if (header.repetition_index != repetition_count) {
        LOG_WARN("Mismatching repetition count in MSC header and segmentation header {}!={}", header.repetition_index, repetition_count);
    }

    // TODO: For MOT body entities the time taken to assemble them can be quite long
    //       Signal the progress of the assembler to a listener for MOT body entities
    auto* assembler_table = m_assembler_tables.find(header.transport_id);
    if (assembler_table == nullptr) {
        assembler_table = &m_assembler_tables.emplace(header.transport_id);
    }

    auto& assembler = GetAssembler(*assembler_table, header.data_group_type);
    if (header.is_last_segment) {
        assembler.SetTotalSegments(header.segment_number+1);
    }
    const bool is_updated = assembler.AddSegment(header.segment_number, data);
    if (!is_updated) {
        return;
    }

    if (!assembler.CheckComplete()) {
        return;
    }
 
    // TODO: Handle other mot data types
    if (header.data_group_type == MOT_Data_Type::UNCOMPRESSED_DIRECTORY) {
        ProcessDirectory(header.transport_id);
    } else if (header.data_group_type == MOT_Data_Type::HEADER) {
        auto header_buf = assembler.GetData();
        MOT_Header_Entity entity_header;
        auto res = ProcessHeader(entity_header, header_buf);
        if (res == std::nullopt) return;
        m_body_headers.insert(header.transport_id, std::move(entity_header)); 
        CheckBodyComplete(header.transport_id);
    } else if (header.data_group_type == MOT_Data_Type::UNSCRAMBLED_BODY) {
        CheckBodyComplete(header.transport_id);
    }
}

MOT_Assembler& MOT_Processor::GetAssembler(MOT_Assembler_Table& table, const MOT_Data_Type type) {
    auto res = table.find(type);
    if (res == table.end()) {
        res = table.insert({type, {}}).first;
    }
    return res->second;
}

bool MOT_Processor::CheckBodyComplete(const mot_transport_id_t transport_id) {
    // DOC: ETSI EN 301 234
    // Clause 5.3.1 Single object transmission (MOT header mode)
    // Figure 12: Repetition on object level (example)
    auto* assembler_table = m_assembler_tables.find(transport_id);
    if (assembler_table == nullptr) {
        return false;
    }
    auto* header = m_body_headers.find(transport_id);
    if (header == nullptr) {
        return false;
    }
    auto& body_assembler = GetAssembler(*assembler_table, MOT_Data_Type::UNSCRAMBLED_BODY);
    if (!body_assembler.CheckComplete()) {
        return false;
    }

    const auto body_buf = body_assembler.GetData();
    if (header->body_size != uint32_t(body_buf.size())) {
        LOG_ERROR("Mismatching body length fields {}!={}", header->body_size, body_buf.size());
        return false;
    }

    MOT_Entity entity;
    entity.transport_id = transport_id;
    entity.body_buf = body_buf;
    entity.header = *header;

    LOG_MESSAGE("Completed a MOT header entity with header={} body={} tid={}", entity.header.header_size, entity.header.body_size, entity.transport_id);
    m_obs_on_entity_complete.Notify(entity);
    return true;
}

bool MOT_Processor::ProcessDirectory(const mot_transport_id_t transport_id) {
    // DOC: ETSI EN 301 234
    // Clause 5.3.2 Multiple object transmissions (MOT directory mode)
    auto* assembler_table = m_assembler_tables.find(transport_id);
    if (assembler_table == nullptr) {
        return false;
    }
    auto& directory_assembler = GetAssembler(*assembler_table, MOT_Data_Type::UNCOMPRESSED_DIRECTORY);
    if (!directory_assembler.CheckComplete()) {
        return false;
    }

    // DOC: ETSI EN 301 234
    // Figure 30: Structure of the MOT directory
    auto buf = directory_assembler.GetData();
    constexpr size_t MIN_HEADER_SIZE = 13;
    if (buf.size() < MIN_HEADER_SIZE) {
        LOG_ERROR("Directory object has insufficient length for header ({}<{})", buf.size(), MIN_HEADER_SIZE);
        return false;
    }
 
    // TODO: Do we need to deal with the data carousel?
    // const uint8_t compression_flag =  (buf[0]  & 0b10000000) >> 7;
    // const uint8_t rfu0             =  (buf[0]  & 0b01000000) >> 6;
    // const uint32_t directory_size  = ((buf[0]  & 0b00111111) << 24) | 
    //                                  ((buf[1]  & 0b11111111) << 16) | 
    //                                  ((buf[2]  & 0b11111111) << 8) | 
    //                                  ((buf[3]  & 0b11111111) << 0);
    const uint16_t total_objects =   ((buf[4]  & 0b11111111) << 8) |
                                     ((buf[5]  & 0b11111111) << 0);
    // const uint32_t carousel_period = ((buf[6]  & 0b11111111) << 16) |
    //                                  ((buf[7]  & 0b11111111) << 8) |
    //                                  ((buf[8]  & 0b11111111) << 0);
    // const uint8_t rfu1             =  (buf[9]  & 0b10000000) >> 7;
    // const uint8_t rfa0             =  (buf[9]  & 0b01100000) >> 5;
    // const uint16_t segment_size    = ((buf[9]  & 0b00011111) << 8) |
    //                                  ((buf[10] & 0b11111111) << 0);
    const uint16_t dir_ext_length  = ((buf[11] & 0b11111111) << 8) |
                                     ((buf[12] & 0b11111111) << 0);
    buf = buf.subspan(MIN_HEADER_SIZE);

    if (buf.size() < dir_ext_length) {
        LOG_ERROR("Directory object has insufficient length for directory extension ({}<{})", buf.size(), dir_ext_length);
        return false;
    }

    // TODO: Clause 7.2.4 List of all MOT parameters in the MOT directory extension
    // auto dir_extension_parameters = buf.first(dir_ext_length);
    buf = buf.subspan(dir_ext_length);
 
    size_t current_directory_entity = 0;
    while (true) {
        constexpr size_t TRANSPORT_ID_SIZE = 2;
        if (buf.size() < TRANSPORT_ID_SIZE) {
            LOG_ERROR("Directory entries buffer has insufficient length ({}<{})", buf.size(), TRANSPORT_ID_SIZE);
            break;
        }
        const uint16_t body_transport_id = (buf[0] << 8) | buf[1];
        buf = buf.subspan(TRANSPORT_ID_SIZE);

        MOT_Header_Entity body_header;
        const auto total_read_opt = ProcessHeader(body_header, buf);
        // terminate reading of all directories entries if we encounter an intermittent error, this is not recoverable
        if (!total_read_opt.has_value()) {
            LOG_ERROR("Directry entry failed to read header, index={}", current_directory_entity);
            break;
        }

        // NOTE: Directory entries seem to be sent very rarely, so we want to be generous about which headers to cache
        m_body_headers.insert(body_transport_id, std::move(body_header));
        auto* body_assembler_table = m_assembler_tables.find(body_transport_id);
        if (body_assembler_table != nullptr) {
            CheckBodyComplete(body_transport_id);
        }

        const size_t total_read = total_read_opt.value();
        assert(total_read <= buf.size());
        buf = buf.subspan(total_read);
        current_directory_entity++;
    }

    if (current_directory_entity != total_objects) {
        LOG_ERROR("Some directory entries were missed ({} != {})", current_directory_entity, total_objects);
    }

    return true;
}

std::optional<size_t> MOT_Processor::ProcessHeader(MOT_Header_Entity& entity, tcb::span<const uint8_t> buf) {
    // DOC: ETSI EN 301 234
    // Clause 5.3.1: Single object transmission (MOT header mode) 
    // Figure 14: Repeated transmission of header information
    // The header consists of the header core and header extension
 
    auto data = buf;
    // DOC: ETSI EN 301 234
    // Clause 6.1: Header core 
    const int TOTAL_HEADER_CORE = 7;
    if (data.size() < TOTAL_HEADER_CORE) {
        LOG_ERROR("Insufficient length for header core ({}<{})", data.size(), TOTAL_HEADER_CORE);
        return std::nullopt;
    }

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
    data = data.subspan(TOTAL_HEADER_CORE);

    entity.body_size = body_size;
    entity.header_size = header_size;
    entity.content_type = content_type;
    entity.content_sub_type = content_sub_type;

    if (header_size < TOTAL_HEADER_CORE) {
        LOG_ERROR("Provided header size is smaller than the header core size ({}<{})", header_size, TOTAL_HEADER_CORE);
        return std::nullopt;
    }

    const size_t header_ext_size = header_size - TOTAL_HEADER_CORE;
    if (data.size() < header_ext_size) {
        LOG_ERROR("Header extension buffer is smaller than header specified size ({}<{})", data.size(), header_ext_size);
        return std::nullopt;
    }
 
    data = data.first(header_ext_size);

    // DOC: ETSI TS 101 756
    // Clause 6: Registered tables in ETSI EN 301 234 (MOT) 
    // Table 17: Content type and content subtypes 
    // Clause 6.2: Header extension 
    while (!data.empty()) {
        // Parameter length indicator
        const uint8_t pli      = (data[0] & 0b11000000) >> 6;
        const uint8_t param_id = (data[0] & 0b00111111) >> 0;
        data = data.subspan(1);

        size_t nb_data_bytes = 0;
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
            if (data.size() < 1) {
                LOG_ERROR("Insufficient length for data field indicator ({}<{})", data.size(), 1);
                break;
            }
            const uint8_t ext_flag = (data[0] & 0b10000000) >> 7;
            if (ext_flag) {
                if (data.size() < 2) {
                    LOG_ERROR("Insufficient length for extended data field indicator ({}<{})", data.size(), 2);
                    break;
                }
                nb_data_bytes = ((data[0] & 0b01111111) << 8) | data[1];
                data = data.subspan(2);
            } else {
                nb_data_bytes =  (data[0] & 0b01111111) << 0;
                data = data.subspan(1);
            }
        }

        if (data.size() < nb_data_bytes) {
            LOG_ERROR("Insufficient length for data field ({}<{}) pli={} param_id={}", data.size(), nb_data_bytes, pli, param_id);
            break;
        }

        auto field = data.first(nb_data_bytes); 
        data = data.subspan(nb_data_bytes);
        ProcessHeaderExtensionParameter(entity, param_id, field);
    }

    return std::optional(header_size);
}

bool MOT_Processor::ProcessHeaderExtensionParameter(MOT_Header_Entity& entity, const uint8_t id, tcb::span<const uint8_t> buf) {
    // DOC: ETSI EN 301 234
    // Clause 6.3: List of all MOT parameters in the MOT header extension 
    // Table 2: Coding of extension parameter 

    // User specific application parameters
    if ((id >= 0b100101) && (id <= 0b111111)) {
        MOT_Header_Extension_Parameter param;
        param.type = id;
        param.data.resize(buf.size());
        std::memcpy(param.data.data(), buf.data(), buf.size());
        entity.user_app_params.push_back(std::move(param));
        return true;
    }

    if (id > 0b111111) {
        LOG_ERROR("[header-ext] Out of table param_id={} length={}", id, buf.size());
        return false;
    }

    #define UNIMPLEMENTED_CASE(_id, name) {\
    case _id:\
        LOG_WARN("[header-ext] Unimplemented param_id={} length={} type=" name, _id, buf.size());\
        return false;\
    }

    switch (id) {
    case 0b001100: return ProcessHeaderExtensionParameter_ContentName(entity, buf);
    case 0b000100: return ProcessHeaderExtensionParameter_ExpireTime(entity, buf);
    case 0b000101: return ProcessHeaderExtensionParameter_TriggerTime(entity, buf);
    // TODO: Implement this
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
        LOG_WARN("[header-ext] Reserved for extension: param_id={} length={}", id, buf.size());
        return false;
    }
    #undef UNIMPLEMENTED_CASE
}

bool MOT_Processor::ProcessHeaderExtensionParameter_ContentName(MOT_Header_Entity& entity, tcb::span<const uint8_t> buf) {
    // DOC: ETSI EN 301 234
    // Clause 6.2.2.1.1: Content name
    if (buf.size() < 2) {
        LOG_ERROR("[header-ext] type=content_name Insufficient length for content name header and data {}<{}", buf.size(), 2);
        return false;
    }

    const uint8_t charset = (buf[0] & 0b11110000) >> 4;
    const uint8_t rfa0    = (buf[0] & 0b00001111) >> 0;
    auto name_buf = buf.subspan(1);
    auto name = std::string_view(reinterpret_cast<const char*>(name_buf.data()), name_buf.size());

    entity.content_name.exists = true;
    entity.content_name.charset = charset;
    entity.content_name.name = name;

    LOG_MESSAGE("[header-ext] type=content_name charset={} rfa0={} name[{}]={}", charset, rfa0, name.length(), entity.content_name.name);
    return true;
}

bool MOT_Processor::ProcessHeaderExtensionParameter_ExpireTime(MOT_Header_Entity& entity, tcb::span<const uint8_t> buf) {
    // NOTE: The expire time field is defined by the following doc
    // DOC: ETSI TS 101 499
    // Clause 6.2.1: General
    // Table 3: MOT Parameters
    // For some reason it is not defined by the expected document
    // DOC: ETSI EN 301 234 
    return ProcessHeaderExtensionParameter_UTCTime(entity.expire_time, buf);
}

bool MOT_Processor::ProcessHeaderExtensionParameter_TriggerTime(MOT_Header_Entity& entity, tcb::span<const uint8_t> buf) {
    return ProcessHeaderExtensionParameter_UTCTime(entity.trigger_time, buf);
}

bool MOT_Processor::ProcessHeaderExtensionParameter_UTCTime(MOT_UTC_Time& entity, tcb::span<const uint8_t> buf) {
    // DOC: ETSI EN 301 234
    // Clause 6.2.4.1: Coding of time parameters 
    constexpr size_t MIN_HEADER_SIZE = 4;
    if (buf.size() < MIN_HEADER_SIZE) {
        LOG_ERROR("[header-ext] type=utc_time Insufficient length for time header and data ({}<{})", buf.size(), MIN_HEADER_SIZE);
        return false;
    }

    const uint8_t validity_flag =  (buf[0] & 0b10000000) >> 7;

    // The entire field is zeroed and trigger time means "now"
    if (!validity_flag) {
        entity.exists = true;
        entity.year = 0;
        entity.month = 0;
        entity.day = 0;
        entity.hours = 0;
        entity.minutes = 0;
        entity.seconds = 0;
        entity.milliseconds = 0;
        LOG_MESSAGE("[header-ext] type=utc_time valid={} datetime=NOW", validity_flag);
        return true;            
    }

    const uint32_t MJD_date = ((buf[0] & 0b01111111) << 10) |
                              ((buf[1] & 0b11111111) << 2)  |
                              ((buf[2] & 0b11000000) >> 6);
    // const uint8_t rfu0      =  (buf[2] & 0b00110000) >> 4;
    const uint8_t UTC_flag  =  (buf[2] & 0b00001000) >> 4;
    const uint8_t hours     = ((buf[2] & 0b00000111) << 2) |
                              ((buf[3] & 0b11000000) >> 6);
    const uint8_t minutes   =  (buf[3] & 0b00111111) >> 0;
    uint8_t seconds = 0;
    uint16_t milliseconds = 0;
    buf = buf.subspan(MIN_HEADER_SIZE);
 
    if (UTC_flag) {
        constexpr size_t UTC_FIELD_SIZE = 2;
        if (buf.size() < UTC_FIELD_SIZE) {
            LOG_ERROR("[header-ext] type=utc_time Insufficient length for time header and long UTC ({}<{})", buf.size(), UTC_FIELD_SIZE);
            return false;
        }
        seconds      =  (buf[0] & 0b11111100) >> 2;
        milliseconds = ((buf[0] & 0b00000011) << 8) | buf[1];
        buf = buf.subspan(UTC_FIELD_SIZE);
    }

    int year, month, day;
    mjd_to_ymd(static_cast<long>(MJD_date), year, month, day);

    entity.exists = true;
    entity.year = year;
    entity.month = month;
    entity.day = day;
    entity.hours = hours;
    entity.minutes = minutes;
    entity.seconds = seconds;
    entity.milliseconds = milliseconds;

    LOG_MESSAGE("[header-ext] type=utc_time valid={} utc={} date={:02}/{:02}/{:04} time={:02}:{:02}:{:02}.{:03}",
        validity_flag, UTC_flag, day, month, year, hours, minutes, seconds, milliseconds);
    return true;
}