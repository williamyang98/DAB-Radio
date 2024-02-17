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

// NOTE: A valid database entry exists when all the required fields are set
// The required fields constraint is also followed in the dab_database_updater.cpp
// when we are regenerating the database from the FIC (fast information channel)

// Data fields
struct Ensemble {
    ensemble_id_t reference = 0;                        // required
    country_id_t country_id = 0;                        // required
    extended_country_id_t extended_country_code = 0;    // required
    std::string label;
    uint8_t nb_services = 0;                            // optional: fig 0/7 provides this
    uint16_t reconfiguration_count = 0;                 // optional: fig 0/7 provides this
    int8_t local_time_offset = 0;                       // Value of this shall be +- 155 (LTO is +-15.5 hours)
    uint8_t international_table_id = 0;                 // table id for programme type strings
    bool is_complete = false;
};

struct Service {
    service_id_t reference = 0;                    
    country_id_t country_id = 0;                        // required 
    extended_country_id_t extended_country_code = 0;  
    std::string label;
    programme_id_t programme_type = 0;    
    language_id_t language = 0;          
    closed_caption_id_t closed_caption = 0;       
    bool is_complete = false;
    explicit Service(const service_id_t _ref) : reference(_ref) {}
};

struct ServiceComponent {
    // NOTE: Two methods to identify a service component
    // Method 1: service_id/SCIdS used together for stream mode
    service_id_t service_reference;   
    service_component_id_t component_id;         
    // Method 2: SCId global identifier used for packet mode
    service_component_global_id_t global_id = 0;          
    subchannel_id_t subchannel_id = 0;                                  // required 
    std::string label;
    TransportMode transport_mode = TransportMode::UNDEFINED;            // required
    AudioServiceType audio_service_type = AudioServiceType::UNDEFINED;  // required for transport stream audio
    DataServiceType data_service_type = DataServiceType::UNDEFINED;     // (optional) for transport stream/packet data - we expect this to be provided but real world data doesn't
    bool is_complete = false;
    explicit ServiceComponent(
        const service_id_t _service_ref, 
        const service_component_id_t _component_id
    ): service_reference(_service_ref), component_id(_component_id) {}
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
    service_id_t service_reference = 0;                 // required
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
    ensemble_id_t reference;      
    country_id_t country_id = 0;           
    bool is_continuous_output = false;
    bool is_geographically_adjacent = false;
    bool is_transmission_mode_I = false;
    freq_t frequency = 0;                               // required
    bool is_complete = false;
    explicit OtherEnsemble(const ensemble_id_t _ref): reference(_ref) {}
};
