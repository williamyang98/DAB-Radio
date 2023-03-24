#pragma once 

#include <stdint.h>
#include <unordered_map>
#include <vector>

#include "./MOT_entities.h"
#include "./MOT_assembler.h"
#include "utility/observable.h"
#include "utility/lru_cache.h"
#include "utility/span.h"

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
enum MOT_Data_Type: uint8_t {
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

// Create either header or directory mode MOT entities from msc MOT segment data groups
class MOT_Processor
{
private:
    // DOC: ETSI EN 301 234
    // Clause 5.3.2.1: Interleaving MOT entities in one MOT stream 
    // NOTE: In MOT directory mode we can encounter multiple parallel transport ids
    //       We use an LRU queue to forget entries that are no longer updated
    LRU_Cache<mot_transport_id_t, MOT_Assembler_Table> assembler_tables;

    // args: entity update
    Observable<MOT_Entity> obs_on_entity_complete;
public:
    MOT_Processor(const int max_transport_objects=10);
    void Process_Segment(const MOT_MSC_Data_Group_Header header, tcb::span<const uint8_t> buf);
    auto& OnEntityComplete(void) { return obs_on_entity_complete; }
private:
    MOT_Assembler_Table& GetAssemblerTable(const mot_transport_id_t transport_id);
    MOT_Assembler& GetAssembler(MOT_Assembler_Table& table, const MOT_Data_Type type);
    bool CheckEntityComplete(const mot_transport_id_t transport_id);
private:
    bool ProcessHeader(MOT_Header_Entity& entity, tcb::span<const uint8_t> buf);
    bool ProcessHeaderExtensionParameter(MOT_Header_Entity& entity, const uint8_t id, tcb::span<const uint8_t> buf);
    bool ProcessHeaderExtensionParameter_ContentName(MOT_Header_Entity& entity, tcb::span<const uint8_t> buf);
    bool ProcessHeaderExtensionParameter_ExpireTime(MOT_Header_Entity& entity, tcb::span<const uint8_t> buf);
    bool ProcessHeaderExtensionParameter_TriggerTime(MOT_Header_Entity& entity, tcb::span<const uint8_t> buf);
    bool ProcessHeaderExtensionParameter_UTCTime(MOT_UTC_Time& entity, tcb::span<const uint8_t> buf);
};


