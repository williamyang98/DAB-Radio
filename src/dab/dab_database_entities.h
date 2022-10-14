#pragma once

#include <stdint.h>
#include <string>
#include <vector>
#include <set>

#include "dab_database_types.h"

// Types
enum TransportMode : uint8_t {      // 2bits
    STREAM_MODE_AUDIO = 0b00,
    STREAM_MODE_DATA = 0b01,
    PACKET_MODE_DATA = 0b11
};

enum AudioServiceType {     // 6bits
    DAB,                    // 0
    DAB_PLUS,               // 63
};

enum DataServiceType {      // 6bits
    TRANSPARENT_CHANNEL,    // 5
    MPEG2,                  // 24
    MOT,                    // 60 (Multimedia Object Transfer)
    PROPRIETARY,            // 63
};

enum EEP_Type {
    TYPE_A, TYPE_B
};

// Data fields
struct Ensemble {
    ensemble_id_t reference = 0;        
    country_id_t country_id = 0;        
    extended_country_id_t extended_country_code = 0;  
    std::string label;
    uint8_t nb_services = 0;            // fig 0/7 provides this
    uint16_t reconfiguration_count = 0; // fig 0/7 provides this
    int local_time_offset = 0;          // Value of this shall be +- 155 (LTO is +-15.5 hours)
    uint8_t international_table_id = 0; // table id for programme type strings
};

struct ServiceComponent;

struct Service {
    const service_id_t reference = 0;        
    country_id_t country_id = 0;
    extended_country_id_t extended_country_code = 0;  
    std::string label;
    programme_id_t programme_type = 0;    
    language_id_t language = 0;          
    closed_caption_id_t closed_caption = 0;       
    Service(const service_id_t _ref) : reference(_ref) {}
};

struct ServiceComponent {
    // NOTE: Two methods to identify a service component
    // Method 1: service_id/SCIdS used together for stream mode
    const service_id_t service_reference;   
    const service_component_id_t component_id;         
    // Method 2: SCId global identifier used for packet mode
    service_component_global_id_t global_id = 0;          
    subchannel_id_t subchannel_id = 0;          
    std::string label;
    TransportMode transport_mode; 
    AudioServiceType audio_service_type;
    DataServiceType data_service_type;    
    ServiceComponent(
        const service_id_t _service_ref, 
        const service_component_id_t _component_id
    ): service_reference(_service_ref), component_id(_component_id) {}
};

struct Subchannel {
    const subchannel_id_t id;                   
    subchannel_addr_t start_address = 0;     
    subchannel_size_t length = 0;  
    bool is_uep = false;
    protection_level_t protection_level = 0;    
    EEP_Type eep_type;              // Type A and B for EEP
    fec_scheme_t fec_scheme = 0;    // used only for packet mode
    Subchannel(const subchannel_id_t _id): id(_id) {}
};

// For frequency or service sharing across different transmissions 
// E.g. A service may be linked to a FM station, etc...
struct LinkService {
    const lsn_t id;                         // linkage set number (LSN)
    bool is_active_link = false;
    bool is_hard_link =  false;
    bool is_international = false;
    service_id_t service_reference = 0;     // LSN is linked to a service
    LinkService(const lsn_t _id): id(_id) {}
};

struct FM_Service {
    const fm_id_t RDS_PI_code;              // the unique identifier
    lsn_t linkage_set_number = 0;
    bool is_time_compensated = false;
    std::set<freq_t> frequencies;         
    FM_Service(const fm_id_t _id): RDS_PI_code(_id) {}
};

struct DRM_Service {
    const drm_id_t drm_code;             
    lsn_t linkage_set_number = 0;
    bool is_time_compensated = false;
    std::set<freq_t> frequencies; 
    DRM_Service(const drm_id_t _id): drm_code(_id) {}
};

struct AMSS_Service {
    const amss_id_t amss_code; 
    bool is_time_compensated = false;
    std::set<freq_t> frequencies;         
    AMSS_Service(const amss_id_t _id): amss_code(_id) {}
};

// other ensemble information
struct OtherEnsemble {
    const ensemble_id_t reference;      
    country_id_t country_id = 0;           
    bool is_continuous_output = false;
    bool is_geographically_adjacent = false;
    bool is_transmission_mode_I = false;
    freq_t frequency = 0;
    OtherEnsemble(const ensemble_id_t _ref): reference(_ref) {}
};
