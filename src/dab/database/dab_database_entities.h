#pragma once

#include <stdint.h>
#include <string>
#include <vector>

#include "./dab_database_types.h"

// Types
enum class TransportMode : uint8_t {    // Value passed in 2bit field
    STREAM_MODE_AUDIO = 0b00,
    STREAM_MODE_DATA = 0b01,
    PACKET_MODE_DATA = 0b11,
    UNDEFINED = 0xFF,
};

enum class AudioServiceType: uint8_t {  // Value passed in 6bit field 
    DAB = 0,
    DAB_PLUS = 63,
    UNDEFINED = 0xFF,
};

enum class DataServiceType: uint8_t {   // Value passed in 6bit field
    TRANSPARENT_CHANNEL = 5,
    MPEG2 = 24,              
    MOT = 60,                           // Multimedia Object Transfer
    PROPRIETARY = 63,        
    UNDEFINED = 0xFF,
};

enum class EEP_Type: uint8_t {
    TYPE_A = 0, 
    TYPE_B = 1,
    UNDEFINED = 0xFF,
};

// DOC: ETSI EN 300 401
// Clause: 6.2.2 FEC sub-channel organization
// Clause: 5.3.5 FEC for MSC packet mode
enum class FEC_Scheme: uint8_t {        // Value passed in 4bit field
    NONE = 0b00,
    REED_SOLOMON = 0b01,
    RFA0 = 0b10,
    RFA1 = 0b11,
    UNDEFINED = 0xFF,
};

enum class UserApplicationType: uint16_t {  // Value passed in 11bit field
    SLIDESHOW = 0x002,
    TPEG = 0x004,
    SPI = 0x007,
    DMB = 0x009,
    FILE_CASTING = 0x00D,
    FIS = 0x00E,
    JOURNALINE = 0x44A,
    UNDEFINED = 0xFFFF,
};

// NOTE: A valid database entry exists when all the required fields are set
// The required fields constraint is also followed in the dab_database_updater.cpp
// when we are regenerating the database from the FIC (fast information channel)

// DOC: ETSI EN 300 401
// Clause: 6.4.1 Ensemble information
struct EnsembleId {
    uint16_t value = 0;
    uint16_t get_unique_identifier() const {
        return value;
    }
    country_id_t get_country_code() const {
        return (value & 0xF000) >> 12;
    }
    ensemble_reference_t get_reference() const {
        return value & 0x0FFF;
    }
    bool operator==(const EnsembleId& other) const {
        return value == other.value;
    }
};

// Data fields
struct Ensemble {
    EnsembleId id;                                      // required
    extended_country_id_t extended_country_code = 0;
    std::string label;
    std::string short_label;
    uint8_t nb_services = 0;                            // optional: fig 0/7 provides this
    uint16_t reconfiguration_count = 0;                 // optional: fig 0/7 provides this
    int8_t local_time_offset = 0;                       // Value of this shall be +- 155 (LTO is +-15.5 hours)
    uint8_t international_table_id = 0;                 // table id for programme type strings
    bool is_complete = false;
};

// DOC: ETSI EN 300 401
// Clause: 6.3.1 Basic service and service component definition
enum class ServiceIdType: uint8_t {
    BITS16 = 0b00,
    BITS32 = 0b01,
    // NOTE: wierd special case where extended country code is provided separately but unique identifier is still 16bits
    // Clause: 8.1.15 Service linking information (fig 0/6)
    // Clause: 8.1.3.2 Country, LTO and International table (fig 0/9)
    BITS24 = 0b10,
    INVALID = 0xFF,
};

struct ServiceId {
    uint32_t value = 0;
    ServiceIdType type = ServiceIdType::INVALID;
    // Clause 6.3.1: The unique identifier is either 16bits or 32bits
    uint32_t get_unique_identifier() const {
        switch (type) {
            case ServiceIdType::BITS16: return value & 0xF'FFF;
            case ServiceIdType::BITS24: return value & 0x00'F'FFF; // exclude the extended country code
            case ServiceIdType::BITS32: return value & 0xFF'F'FFFFF;
            default: return 0;
        }
    }
    extended_country_id_t get_extended_country_code() const {
        switch (type) {
            case ServiceIdType::BITS16: return 0x00;
            case ServiceIdType::BITS24: return (value & 0xFF'0'000) >> 16;
            case ServiceIdType::BITS32: return (value & 0xFF'0'00000) >> 24;
            default: return 0;
        }
    }
    country_id_t get_country_code() const {
        switch (type) {
            case ServiceIdType::BITS16: return (value & 0xF'000) >> 12;
            case ServiceIdType::BITS24: return (value & 0x00'F'000) >> 12;
            case ServiceIdType::BITS32: return (value & 0x00'F'00000) >> 20;
            default: return 0;
        }
    }
    service_reference_t get_reference() const {
        switch (type) {
            case ServiceIdType::BITS16: return value & 0x0'FFF;
            case ServiceIdType::BITS24: return value & 0x00'0'FFF;
            case ServiceIdType::BITS32: return value & 0x00'0'FFFFF;
            default: return 0;
        }
    }
    bool operator==(const ServiceId& other) const {
        return (value == other.value) && (type == other.type);
    }
};

struct Service {
    ServiceId id; // required
    std::string label;
    std::string short_label;
    programme_id_t programme_type = 0;    
    bool is_complete = false;
    explicit Service(const ServiceId _id) : id(_id) {}
};

struct ServiceComponent {
    // NOTE: Two methods to identify a service component
    // Method 1: service_id/SCIdS used together for stream mode
    ServiceId service_id;
    service_component_id_t component_id;         
    // Method 2: SCId global identifier used for packet mode
    service_component_global_id_t global_id = 0xFFFF;                   // required for transport packet data
    subchannel_id_t subchannel_id = 0;                                  // required 
    packet_addr_t packet_address = 0;                                   // required for transport packet data
    std::string label;
    std::string short_label;
    language_id_t language = 0;
    std::vector<user_application_type_t> application_types;             // required for transport stream/packet data. (optional) for stream audio (XPAD often used for slideshows)
    TransportMode transport_mode = TransportMode::UNDEFINED;            // required
    AudioServiceType audio_service_type = AudioServiceType::UNDEFINED;  // required for transport stream audio
    DataServiceType data_service_type = DataServiceType::UNDEFINED;     // required for transport stream/packet data
    bool is_complete = false;
    explicit ServiceComponent(
        const ServiceId _service_id, 
        const service_component_id_t _component_id
    ): service_id(_service_id), component_id(_component_id) {}
};

struct Subchannel {
    subchannel_id_t id; 
    subchannel_addr_t start_address = 0;                 // required
    subchannel_size_t length = 0;                        // required
    bool is_uep = false;                                 // required
    uep_protection_index_t uep_prot_index = 0;           // required for UEP
    eep_protection_level_t eep_prot_level = 0;           // required for EEP
    EEP_Type eep_type = EEP_Type::UNDEFINED;             // required for EEP
    FEC_Scheme fec_scheme = FEC_Scheme::UNDEFINED;       // (optional) used only for packet mode
    bool is_complete = false;
    explicit Subchannel(const subchannel_id_t _id): id(_id) {}
};

// For frequency or service sharing across different transmissions 
// E.g. A service may be linked to a FM station, etc...
struct LinkService {
    lsn_t id;                                     // linkage set number (LSN)
    bool is_active_link = false;                        
    bool is_hard_link =  false;
    bool is_international = false;
    ServiceId service_id;                       // required
    bool is_complete = false;
    explicit LinkService(const lsn_t _id): id(_id) {}
};

struct FM_Service {
    fm_id_t RDS_PI_code;                           
    lsn_t linkage_set_number = 0;                       // required
    bool is_time_compensated = false;
    std::vector<freq_t> frequencies;                       // required
    bool is_complete = false;
    explicit FM_Service(const fm_id_t _id): RDS_PI_code(_id) {}
};

struct DRM_Service {
    drm_id_t drm_code;             
    lsn_t linkage_set_number = 0;                       // required
    bool is_time_compensated = false;
    std::vector<freq_t> frequencies;                       // required
    bool is_complete = false;
    explicit DRM_Service(const drm_id_t _id): drm_code(_id) {}
};

struct AMSS_Service {
    amss_id_t amss_code; 
    bool is_time_compensated = false;
    std::vector<freq_t> frequencies;                       // required     
    bool is_complete = false;
    explicit AMSS_Service(const amss_id_t _id): amss_code(_id) {}
};

// other ensemble information
struct OtherEnsemble {
    EnsembleId id;      
    bool is_continuous_output = false;
    bool is_geographically_adjacent = false;
    bool is_transmission_mode_I = false;
    freq_t frequency = 0;                               // required
    bool is_complete = false;
    explicit OtherEnsemble(const EnsembleId _id): id(_id) {}
};
