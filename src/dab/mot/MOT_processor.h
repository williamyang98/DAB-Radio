#pragma once 

#include <stddef.h>
#include <stdint.h>
#include <optional>
#include <unordered_map>
#include "utility/lru_cache.h"
#include "utility/observable.h"
#include "utility/span.h"
#include "./MOT_assembler.h"
#include "./MOT_entities.h"

// DOC: ETSI EN 301 234 
// Clause 5.2.2: X-PAD
// Data group type field in MSC XPAD header
// Type | Description
//   3  | MOT header 
//   4  | Unscrambled MOT body
//   6  | Uncompressed MOT directory
//   7  | Compressed MOT directory
//   1  | ECM/EMM data (conditional access)
//   5  | Scrambled MOT body (conditional access)
enum class MOT_Data_Type: uint8_t {
    ECM_EMM_DATA = 1,
    HEADER = 3,
    UNSCRAMBLED_BODY = 4,
    SCRAMBLED_BODY = 5,
    UNCOMPRESSED_DIRECTORY = 6,
    COMPRESSED_DIRECTORY = 7
};

struct MOT_MSC_Data_Group_Header {
    MOT_Data_Type data_group_type;
    uint8_t continuity_index;
    uint8_t repetition_index;
    bool is_last_segment;
    uint16_t segment_number;
    mot_transport_id_t transport_id;
};

typedef std::unordered_map<MOT_Data_Type, MOT_Assembler> MOT_Assembler_Table;

// Create MOT entities from MSC data groups
class MOT_Processor
{
private:
    // DOC: ETSI EN 301 234
    // Clause 5.3.2.1: Interleaving MOT entities in one MOT stream 
    LRU_Cache<mot_transport_id_t, MOT_Assembler_Table> m_assembler_tables;
    LRU_Cache<mot_transport_id_t, MOT_Header_Entity> m_body_headers;
    Observable<MOT_Entity> m_obs_on_entity_complete;
public:
    // Header entities are quite small so we set a generous upper bound
    explicit MOT_Processor(const size_t max_transport_entities=20, const size_t max_header_entities=200);
    void Process_MSC_Data_Group(const MOT_MSC_Data_Group_Header header, tcb::span<const uint8_t> buf);
    auto& OnEntityComplete(void) { return m_obs_on_entity_complete; }
private:
    MOT_Assembler& GetAssembler(MOT_Assembler_Table& table, const MOT_Data_Type type);
    bool CheckBodyComplete(const mot_transport_id_t transport_id);
    bool ProcessDirectory(const mot_transport_id_t transport_id);
    std::optional<size_t> ProcessHeader(MOT_Header_Entity& entity, tcb::span<const uint8_t> buf);
    bool ProcessHeaderExtensionParameter(MOT_Header_Entity& entity, const uint8_t id, tcb::span<const uint8_t> buf);
    bool ProcessHeaderExtensionParameter_ContentName(MOT_Header_Entity& entity, tcb::span<const uint8_t> buf);
    bool ProcessHeaderExtensionParameter_ExpireTime(MOT_Header_Entity& entity, tcb::span<const uint8_t> buf);
    bool ProcessHeaderExtensionParameter_TriggerTime(MOT_Header_Entity& entity, tcb::span<const uint8_t> buf);
    bool ProcessHeaderExtensionParameter_UTCTime(MOT_UTC_Time& entity, tcb::span<const uint8_t> buf);
};


