#include "dab_database_updater.h"

#include <string>
#include <string.h>

#include "easylogging++.h"
#include "fmt/core.h"

#define LOG_MESSAGE(...) CLOG(INFO, "db-updater") << fmt::format(__VA_ARGS__)
#define LOG_ERROR(...) CLOG(ERROR, "db-updater") << fmt::format(__VA_ARGS__)

// macros for definining a form field
// ALT = if property name of data structure is different from argument name
// FULL = pass in inline code to run if the field is updating
#define FORM_FIELD_ALT_MACRO_FULL(prop, flag, value, on_pass) {\
    if (dirty_field & flag) {\
        if (data->prop != value) {\
            LOG_ERROR("{} conflict because of value mismatch", #flag);\
            OnConflict();\
            return UpdateResult::CONFLICT;\
        }\
        return UpdateResult::NO_CHANGE;\
    }\
    dirty_field |= flag;\
    data->prop = value;\
    on_pass;\
    CheckIsComplete();\
    OnUpdate();\
    return UpdateResult::SUCCESS;\
}

#define FORM_FIELD_MACRO_FULL(prop, flag, on_pass) FORM_FIELD_ALT_MACRO_FULL(prop, flag, prop, on_pass)
#define FORM_FIELD_MACRO(prop, flag) FORM_FIELD_MACRO_FULL(prop, flag, {})
#define FORM_FIELD_ALT_MACRO(prop, flag, value) FORM_FIELD_ALT_MACRO_FULL(prop, flag, value, {})

#define FORM_FIELD_STRING_MACRO(prop, flag, buf, N) {\
    if (dirty_field & flag) {\
        std::string& x = data->prop;\
        auto* buf0 = x.data();\
        const auto N0 = x.length();\
        if (N != N0) {\
            LOG_ERROR("{} conflict because of length mismatch ({}/{})", #flag, N, N0);\
            OnConflict();\
            return UpdateResult::CONFLICT;\
        }\
        if (strncmp(buf0, (const char*)(buf), N) != 0) {\
            LOG_ERROR("{} conflict because of content differnce", #flag);\
            OnConflict();\
            return UpdateResult::CONFLICT;\
        }\
        return UpdateResult::NO_CHANGE;\
    }\
    dirty_field |= flag;\
    data->prop = std::string((const char*)(buf), N);\
    CheckIsComplete();\
    OnUpdate();\
    return UpdateResult::SUCCESS;\
}

// Ensemble form
const uint8_t ENSEMBLE_FLAG_REFERENCE   = 0b10000000;
const uint8_t ENSEMBLE_FLAG_COUNTRY_ID  = 0b01000000;
const uint8_t ENSEMBLE_FLAG_ECC         = 0b00100000;
const uint8_t ENSEMBLE_FLAG_LABEL       = 0b00010000;
const uint8_t ENSEMBLE_FLAG_NB_SERVICES = 0b00001000;
const uint8_t ENSEMBLE_FLAG_RCOUNT      = 0b00000100;
const uint8_t ENSEMBLE_FLAG_LTO         = 0b00000010;
const uint8_t ENSEMBLE_FLAG_INTER_TABLE = 0b00000001;
const uint8_t ENSEMBLE_FLAG_REQUIRED    = 0b11100001;

UpdateResult EnsembleUpdater::SetReference(const ensemble_id_t reference) {
    FORM_FIELD_MACRO(reference, ENSEMBLE_FLAG_REFERENCE);
}

UpdateResult EnsembleUpdater::SetCountryID(const country_id_t country_id) {
    FORM_FIELD_MACRO(country_id, ENSEMBLE_FLAG_COUNTRY_ID);
}

UpdateResult EnsembleUpdater::SetExtendedCountryCode(const extended_country_id_t extended_country_code) {
    // 0x00 is a NULL extended country code
    // this occurs if the packet doesn't define it
    if (extended_country_code == 0x00) {
        return UpdateResult::NO_CHANGE;
    }
    FORM_FIELD_MACRO(extended_country_code, ENSEMBLE_FLAG_ECC);
}

UpdateResult EnsembleUpdater::SetLabel(const uint8_t* buf, const int N) {
    FORM_FIELD_STRING_MACRO(label, ENSEMBLE_FLAG_LABEL, buf, N);
}

UpdateResult EnsembleUpdater::SetNumberServices(const uint8_t nb_services) {
    FORM_FIELD_MACRO(nb_services, ENSEMBLE_FLAG_NB_SERVICES);
}

UpdateResult EnsembleUpdater::SetReconfigurationCount(const uint16_t reconfiguration_count) {
    FORM_FIELD_MACRO(reconfiguration_count, ENSEMBLE_FLAG_RCOUNT);
}

UpdateResult EnsembleUpdater::SetLocalTimeOffset(const int local_time_offset) {
    FORM_FIELD_MACRO(local_time_offset, ENSEMBLE_FLAG_LTO);
}

UpdateResult EnsembleUpdater::SetInternationalTableID(const uint8_t international_table_id) {
    FORM_FIELD_MACRO(international_table_id, ENSEMBLE_FLAG_INTER_TABLE);
}

bool EnsembleUpdater::IsComplete() {
    return (dirty_field & ENSEMBLE_FLAG_REQUIRED) == ENSEMBLE_FLAG_REQUIRED;
}

// Service form
const uint8_t SERVICE_FLAG_COUNTRY_ID   = 0b10000000;
const uint8_t SERVICE_FLAG_ECC          = 0b01000000;
const uint8_t SERVICE_FLAG_LABEL        = 0b00100000;
const uint8_t SERVICE_FLAG_PROGRAM_TYPE = 0b00010000;
const uint8_t SERVICE_FLAG_LANGUAGE     = 0b00001000;
const uint8_t SERVICE_FLAG_CLOSED_CAP   = 0b00000100;
const uint8_t SERVICE_FLAG_REQUIRED     = 0b10000000;

UpdateResult ServiceUpdater::SetCountryID(const country_id_t country_id) {
    FORM_FIELD_MACRO(country_id, SERVICE_FLAG_COUNTRY_ID);
}

UpdateResult ServiceUpdater::SetExtendedCountryCode(const extended_country_id_t extended_country_code) {
    if (extended_country_code == 0x00) {
        return UpdateResult::NO_CHANGE;
    }
    FORM_FIELD_MACRO(extended_country_code, SERVICE_FLAG_ECC);
}

UpdateResult ServiceUpdater::SetLabel(const uint8_t* buf, const int N) {
    FORM_FIELD_STRING_MACRO(label, SERVICE_FLAG_LABEL, buf, N);
}

UpdateResult ServiceUpdater::SetProgrammeType(const programme_id_t programme_type) {
    FORM_FIELD_MACRO(programme_type, SERVICE_FLAG_PROGRAM_TYPE);
}

UpdateResult ServiceUpdater::SetLanguage(const language_id_t language) {
    FORM_FIELD_MACRO(language, SERVICE_FLAG_LANGUAGE);
}

UpdateResult ServiceUpdater::SetClosedCaption(const closed_caption_id_t closed_caption) {
    FORM_FIELD_MACRO(closed_caption, SERVICE_FLAG_CLOSED_CAP);
}

bool ServiceUpdater::IsComplete() {
    return (dirty_field & SERVICE_FLAG_REQUIRED) == SERVICE_FLAG_REQUIRED;
}

// Service component form
const uint8_t SERVICE_COMPONENT_FLAG_LABEL          = 0b10000000;
const uint8_t SERVICE_COMPONENT_FLAG_TRANSPORT_MODE = 0b01000000;
const uint8_t SERVICE_COMPONENT_FLAG_AUDIO_TYPE     = 0b00100000;
const uint8_t SERVICE_COMPONENT_FLAG_DATA_TYPE      = 0b00010000;
const uint8_t SERVICE_COMPONENT_FLAG_SUBCHANNEL     = 0b00001000;
const uint8_t SERVICE_COMPONENT_FLAG_GLOBAL_ID      = 0b00000100;
// two different set of fields required between audio and data
const uint8_t SERVICE_COMPONENT_FLAG_REQUIRED_AUDIO = 0b01101000;
const uint8_t SERVICE_COMPONENT_FLAG_REQUIRED_DATA  = 0b01011000;

UpdateResult ServiceComponentUpdater::SetLabel(const uint8_t* buf, const int N) {
    FORM_FIELD_STRING_MACRO(
        label, SERVICE_COMPONENT_FLAG_LABEL,
        buf, N);
}

UpdateResult ServiceComponentUpdater::SetTransportMode(const TransportMode transport_mode) {
    // If the data type was defined by we have an audio transport mode
    if ((dirty_field & SERVICE_COMPONENT_FLAG_DATA_TYPE) &&
        (transport_mode == TransportMode::STREAM_MODE_AUDIO)) 
    {
        OnConflict();
        return UpdateResult::CONFLICT;
    }

    FORM_FIELD_MACRO(transport_mode, SERVICE_COMPONENT_FLAG_TRANSPORT_MODE);
}

UpdateResult ServiceComponentUpdater::SetAudioServiceType(const AudioServiceType audio_service_type) {
    const auto res = SetTransportMode(TransportMode::STREAM_MODE_AUDIO);
    if (res == UpdateResult::CONFLICT) {
        OnConflict();
        return UpdateResult::CONFLICT;
    }

    if (dirty_field & SERVICE_COMPONENT_FLAG_DATA_TYPE) {
        OnConflict();
        return UpdateResult::CONFLICT;
    }

    FORM_FIELD_MACRO(audio_service_type, SERVICE_COMPONENT_FLAG_AUDIO_TYPE);
}

UpdateResult ServiceComponentUpdater::SetDataServiceType(const DataServiceType data_service_type) {
    // only possible in stream or packet data mode
    if (dirty_field & SERVICE_COMPONENT_FLAG_AUDIO_TYPE) {
        OnConflict();
        return UpdateResult::CONFLICT;
    }

    FORM_FIELD_MACRO(data_service_type, SERVICE_COMPONENT_FLAG_DATA_TYPE);
}

UpdateResult ServiceComponentUpdater::SetSubchannel(const subchannel_id_t subchannel_id) {
    FORM_FIELD_MACRO_FULL(subchannel_id, SERVICE_COMPONENT_FLAG_SUBCHANNEL, {
        parent->GetDatabase()->CreateLink_ServiceComponent_Subchannel(
            data->service_reference, data->component_id,
            subchannel_id);
    });
}

UpdateResult ServiceComponentUpdater::SetGlobalID(const service_component_global_id_t global_id) {
    FORM_FIELD_MACRO_FULL(global_id, SERVICE_COMPONENT_FLAG_GLOBAL_ID, {
        parent->GetDatabase()->CreateLink_ServiceComponent_Global(
            data->service_reference, data->component_id,
            global_id);
    });
}

uint32_t ServiceComponentUpdater::GetServiceReference() {
    return data->service_reference;
}

bool ServiceComponentUpdater::IsComplete() {
    // Can only determine completeness if we know the transport mode
    if (!(dirty_field & SERVICE_COMPONENT_FLAG_TRANSPORT_MODE)) {
        return false;
    }
    const bool audio_complete = (dirty_field & SERVICE_COMPONENT_FLAG_REQUIRED_AUDIO) == SERVICE_COMPONENT_FLAG_REQUIRED_AUDIO;
    const bool data_complete  = (dirty_field & SERVICE_COMPONENT_FLAG_REQUIRED_DATA) == SERVICE_COMPONENT_FLAG_REQUIRED_DATA;
    if (data->transport_mode == TransportMode::STREAM_MODE_AUDIO) {
        return audio_complete;
    } 
    return data_complete;
}

// Subchannel form
const uint8_t SUBCHANNEL_FLAG_START_ADDRESS     = 0b10000000;
const uint8_t SUBCHANNEL_FLAG_LENGTH            = 0b01000000;
const uint8_t SUBCHANNEL_FLAG_IS_UEP            = 0b00100000;
const uint8_t SUBCHANNEL_FLAG_UEP_PROT_INDEX    = 0b00010000;
const uint8_t SUBCHANNEL_FLAG_EEP_PROT_LEVEL    = 0b00001000;
const uint8_t SUBCHANNEL_FLAG_EEP_TYPE          = 0b00000100;
const uint8_t SUBCHANNEL_FLAG_FEC_SCHEME        = 0b00000010;
const uint8_t SUBCHANNEL_FLAG_REQUIRED_UEP      = 0b11110000;
const uint8_t SUBCHANNEL_FLAG_REQUIRED_EEP      = 0b11101100;

UpdateResult SubchannelUpdater::SetStartAddress(const subchannel_addr_t start_address) {
    FORM_FIELD_MACRO(start_address, SUBCHANNEL_FLAG_START_ADDRESS);
}

UpdateResult SubchannelUpdater::SetLength(const subchannel_size_t length) {
    FORM_FIELD_MACRO(length, SUBCHANNEL_FLAG_LENGTH); 
}

UpdateResult SubchannelUpdater::SetIsUEP(const bool is_uep) {
    FORM_FIELD_MACRO(is_uep, SUBCHANNEL_FLAG_IS_UEP);
}

UpdateResult SubchannelUpdater::SetUEPProtIndex(const uep_protection_index_t uep_prot_index) {
    const auto res = SetIsUEP(true);
    if (res == UpdateResult::CONFLICT) {
        return UpdateResult::CONFLICT;
    }
    FORM_FIELD_MACRO(uep_prot_index, SUBCHANNEL_FLAG_UEP_PROT_INDEX);
}

UpdateResult SubchannelUpdater::SetEEPProtLevel(const eep_protection_level_t eep_prot_level) {
    const auto res = SetIsUEP(false);
    if (res == UpdateResult::CONFLICT) {
        return UpdateResult::CONFLICT;
    }
    FORM_FIELD_MACRO(eep_prot_level, SUBCHANNEL_FLAG_EEP_PROT_LEVEL);
}

UpdateResult SubchannelUpdater::SetEEPType(const EEP_Type eep_type) {
    const auto res = SetIsUEP(false);
    if (res == UpdateResult::CONFLICT) {
        return UpdateResult::CONFLICT;
    }
    FORM_FIELD_MACRO(eep_type, SUBCHANNEL_FLAG_EEP_TYPE);
}

UpdateResult SubchannelUpdater::SetFECScheme(const fec_scheme_t fec_scheme) {
    FORM_FIELD_MACRO(fec_scheme, SUBCHANNEL_FLAG_FEC_SCHEME);
}

bool SubchannelUpdater::IsComplete() {
    // Cant tell if it is complete since it depends on subchannel protection type
    if (!(dirty_field & SUBCHANNEL_FLAG_IS_UEP)) {
        return false;
    }
    const bool eep = (dirty_field & SUBCHANNEL_FLAG_REQUIRED_EEP) == SUBCHANNEL_FLAG_REQUIRED_EEP;
    const bool uep = (dirty_field & SUBCHANNEL_FLAG_REQUIRED_UEP) == SUBCHANNEL_FLAG_REQUIRED_UEP;
    return data->is_uep ? uep : eep;
}

// link service form
const uint8_t LINK_FLAG_ACTIVE          = 0b10000000;
const uint8_t LINK_FLAG_HARD            = 0b01000000;
const uint8_t LINK_FLAG_INTERNATIONAL   = 0b00100000;
const uint8_t LINK_FLAG_SERVICE_REF     = 0b00010000;
const uint8_t LINK_FLAG_REQUIRED        = 0b00010000;

UpdateResult LinkServiceUpdater::SetIsActiveLink(const bool is_active_link) {
    FORM_FIELD_MACRO(is_active_link, LINK_FLAG_ACTIVE);
}

UpdateResult LinkServiceUpdater::SetIsHardLink(const bool is_hard_link) {
    FORM_FIELD_MACRO(is_hard_link, LINK_FLAG_HARD);
}

UpdateResult LinkServiceUpdater::SetIsInternational(const bool is_international) {
    FORM_FIELD_MACRO(is_international, LINK_FLAG_INTERNATIONAL);
}

UpdateResult LinkServiceUpdater::SetServiceReference(const service_id_t service_reference) {
    FORM_FIELD_MACRO_FULL(service_reference, LINK_FLAG_SERVICE_REF, {
        parent->GetDatabase()->CreateLink_Service_LSN(
            service_reference,
            data->id);
    });
}

service_id_t LinkServiceUpdater::GetServiceReference() {
    return data->service_reference;
}

bool LinkServiceUpdater::IsComplete() {
    return (dirty_field & LINK_FLAG_REQUIRED) == LINK_FLAG_REQUIRED;
}

// fm service form
const uint8_t FM_FLAG_LSN       = 0b10000000;
const uint8_t FM_FLAG_TIME_COMP = 0b01000000;
const uint8_t FM_FLAG_FREQ      = 0b00100000;
const uint8_t FM_FLAG_REQUIRED  = 0b10100000;

UpdateResult FM_ServiceUpdater::SetLinkageSetNumber(const lsn_t linkage_set_number) {
    FORM_FIELD_MACRO_FULL(linkage_set_number, FM_FLAG_LSN, {
        parent->GetDatabase()->CreateLink_FM_Service(
            linkage_set_number,
            data->RDS_PI_code);
    });
}

UpdateResult FM_ServiceUpdater::SetIsTimeCompensated(const bool is_time_compensated) {
    FORM_FIELD_MACRO(is_time_compensated, FM_FLAG_TIME_COMP);
}

UpdateResult FM_ServiceUpdater::AddFrequency(const freq_t frequency) {
    const auto [_, is_added] = (data->frequencies).insert(frequency);
    dirty_field |= FM_FLAG_FREQ;
    CheckIsComplete();
    if (is_added) {
        OnUpdate();
    }
    return is_added ? SUCCESS : NO_CHANGE;
}

bool FM_ServiceUpdater::IsComplete() {
    return (dirty_field & FM_FLAG_REQUIRED) == FM_FLAG_REQUIRED;
}

// drm service form
const uint8_t DRM_FLAG_LSN       = 0b10000000;
const uint8_t DRM_FLAG_TIME_COMP = 0b01000000;
const uint8_t DRM_FLAG_FREQ      = 0b00100000;
const uint8_t DRM_FLAG_REQUIRED  = 0b10100000;

UpdateResult DRM_ServiceUpdater::SetLinkageSetNumber(const lsn_t linkage_set_number) {
    FORM_FIELD_MACRO_FULL(linkage_set_number, DRM_FLAG_LSN, {
        parent->GetDatabase()->CreateLink_DRM_Service(
            linkage_set_number,
            data->drm_code);
    });
}

UpdateResult DRM_ServiceUpdater::SetIsTimeCompensated(const bool is_time_compensated) {
    FORM_FIELD_MACRO(is_time_compensated, DRM_FLAG_TIME_COMP);
}

UpdateResult DRM_ServiceUpdater::AddFrequency(const freq_t frequency) {
    const auto [_, is_added] = (data->frequencies).insert(frequency);
    dirty_field |= DRM_FLAG_FREQ;
    CheckIsComplete();
    if (is_added) {
        OnUpdate();
    }
    return is_added ? SUCCESS : NO_CHANGE;
}

bool DRM_ServiceUpdater::IsComplete() {
    return (dirty_field & DRM_FLAG_REQUIRED) == DRM_FLAG_REQUIRED;
}

// amss service form
const uint8_t AMSS_FLAG_TIME_COMP = 0b10000000;
const uint8_t AMSS_FLAG_FREQ      = 0b01000000;
const uint8_t AMSS_FLAG_REQUIRED  = 0b01000000;

UpdateResult AMSS_ServiceUpdater::SetIsTimeCompensated(const bool is_time_compensated) {
    FORM_FIELD_MACRO(is_time_compensated, AMSS_FLAG_TIME_COMP);
}

UpdateResult AMSS_ServiceUpdater::AddFrequency(const freq_t frequency) {
    const auto [_, is_added] = (data->frequencies).insert(frequency);
    dirty_field |= AMSS_FLAG_FREQ;
    CheckIsComplete();
    if (is_added) {
        OnUpdate();
    }
    return is_added ? SUCCESS : NO_CHANGE;
}

bool AMSS_ServiceUpdater::IsComplete() {
    return (dirty_field & AMSS_FLAG_REQUIRED) == AMSS_FLAG_REQUIRED;
}

// other ensemble form
const uint8_t OE_FLAG_COUNTRY_ID = 0b10000000;
const uint8_t OE_FLAG_CONT_OUT   = 0b01000000;
const uint8_t OE_FLAG_GEO_ADJ    = 0b00100000;
const uint8_t OE_FLAG_MODE_I     = 0b00010000;
const uint8_t OE_FLAG_FREQ       = 0b00001000;
const uint8_t OE_FLAG_REQUIRED   = 0b00001000;

UpdateResult OtherEnsembleUpdater::SetCountryID(const country_id_t country_id) {
    FORM_FIELD_MACRO(country_id, OE_FLAG_COUNTRY_ID);
}

UpdateResult OtherEnsembleUpdater::SetIsContinuousOutput(const bool is_continuous_output) {
    FORM_FIELD_MACRO(is_continuous_output, OE_FLAG_CONT_OUT);
}

UpdateResult OtherEnsembleUpdater::SetIsGeographicallyAdjacent(const bool is_geographically_adjacent) {
    FORM_FIELD_MACRO(is_geographically_adjacent, OE_FLAG_GEO_ADJ);
}

UpdateResult OtherEnsembleUpdater::SetIsTransmissionModeI(const bool is_transmission_mode_I) {
    FORM_FIELD_MACRO(is_transmission_mode_I, OE_FLAG_MODE_I);
}

UpdateResult OtherEnsembleUpdater::SetFrequency(const freq_t frequency) {
    FORM_FIELD_MACRO(frequency, OE_FLAG_FREQ);
}

bool OtherEnsembleUpdater::IsComplete() {
    return (dirty_field & OE_FLAG_REQUIRED) == OE_FLAG_REQUIRED;
}

// updater parent
DAB_Database_Updater::DAB_Database_Updater(DAB_Database* _db)
: db(_db), ensemble_updater(_db->GetEnsemble())
{
    ensemble_updater.BindParent(this);
    all_updaters.push_back(&ensemble_updater);
}

void DAB_Database_Updater::SignalComplete() {
    stats.nb_completed++;
    stats.nb_pending--;
    LOG_MESSAGE("pending={} complete={} updates={} total={} conflicts={}", 
        stats.nb_pending, stats.nb_completed, stats.nb_updates, stats.nb_total, stats.nb_conflicts);
}

void DAB_Database_Updater::SignalPending() {
    stats.nb_pending++;
    stats.nb_total++;
    LOG_MESSAGE("pending={} complete={} updates={} total={} conflicts={}", 
        stats.nb_pending, stats.nb_completed, stats.nb_updates, stats.nb_total, stats.nb_conflicts);
}

void DAB_Database_Updater::SignalConflict() {
    stats.nb_conflicts++;
    LOG_ERROR("pending={} complete={} updates={} total={} conflicts={}", 
        stats.nb_pending, stats.nb_completed, stats.nb_updates, stats.nb_total, stats.nb_conflicts);
}

void DAB_Database_Updater::SignalUpdate() {
    stats.nb_updates++;
    LOG_MESSAGE("pending={} complete={} updates={} total={} conflicts={}", 
        stats.nb_pending, stats.nb_completed, stats.nb_updates, stats.nb_total, stats.nb_conflicts);
}

EnsembleUpdater* DAB_Database_Updater::GetEnsembleUpdater() {
    return &ensemble_updater;
}

// Create the instance
ServiceUpdater* DAB_Database_Updater::GetServiceUpdater(
    const service_id_t service_ref) 
{
    auto* service = db->GetService(service_ref, true);
    auto res = service_updaters.find(service_ref);
    if (res == service_updaters.end()) {
        res = service_updaters.insert({service_ref, service}).first;
        (res->second).BindParent(this);
        all_updaters.push_back(&(res->second));
    }
    return &(res->second);
}

ServiceComponentUpdater* DAB_Database_Updater::GetServiceComponentUpdater_Service(
    const service_id_t service_ref, const service_component_id_t component_id) 
{
    auto* service_component = db->GetServiceComponent(service_ref, component_id, true);
    const auto key = std::pair<service_id_t, service_component_id_t>(
        service_ref, component_id);

    auto res = service_component_updaters.find(key);
    if (res == service_component_updaters.end()) {
        res = service_component_updaters.insert({key, service_component}).first;
        (res->second).BindParent(this);
        all_updaters.push_back(&(res->second));
    }
    return &(res->second);
}

SubchannelUpdater* DAB_Database_Updater::GetSubchannelUpdater(
    const subchannel_id_t subchannel_id) 
{
    auto* subchannel = db->GetSubchannel(subchannel_id, true);
    auto res = subchannel_updaters.find(subchannel_id);
    if (res == subchannel_updaters.end()) {
        res = subchannel_updaters.insert({subchannel_id, subchannel}).first;
        (res->second).BindParent(this);
        all_updaters.push_back(&(res->second));
    }
    return &(res->second);
}

LinkServiceUpdater* DAB_Database_Updater::GetLinkServiceUpdater(
    const lsn_t link_service_number) 
{
    auto* link_service = db->GetLinkService(link_service_number, true);
    auto res = link_service_updaters.find(link_service_number);
    if (res == link_service_updaters.end()) {
        res = link_service_updaters.insert({link_service_number, link_service}).first;
        (res->second).BindParent(this);
        all_updaters.push_back(&(res->second));
    }
    return &(res->second);
}

FM_ServiceUpdater* DAB_Database_Updater::GetFMServiceUpdater(
    const fm_id_t RDS_PI_code) 
{
    auto* fm_service = db->Get_FM_Service(RDS_PI_code, true);
    auto res = fm_service_updaters.find(RDS_PI_code);
    if (res == fm_service_updaters.end()) {
        res = fm_service_updaters.insert({RDS_PI_code, fm_service}).first;
        (res->second).BindParent(this);
        all_updaters.push_back(&(res->second));
    }
    return &(res->second);
}

DRM_ServiceUpdater* DAB_Database_Updater::GetDRMServiceUpdater(
    const drm_id_t drm_code) 
{
    auto* drm_service = db->Get_DRM_Service(drm_code, true);
    auto res = drm_service_updaters.find(drm_code);
    if (res == drm_service_updaters.end()) {
        res = drm_service_updaters.insert({drm_code, drm_service}).first;
        (res->second).BindParent(this);
        all_updaters.push_back(&(res->second));
    }
    return &(res->second);
}

AMSS_ServiceUpdater* DAB_Database_Updater::GetAMSS_ServiceUpdater(
    const amss_id_t amss_code) 
{
    auto* amss_service = db->Get_AMSS_Service(amss_code, true);
    auto res = amss_service_updaters.find(amss_code);
    if (res == amss_service_updaters.end()) {
        res = amss_service_updaters.insert({amss_code, amss_service}).first;
        (res->second).BindParent(this);
        all_updaters.push_back(&(res->second));
    }
    return &(res->second);
}

OtherEnsembleUpdater* DAB_Database_Updater::GetOtherEnsemble(
    const ensemble_id_t ensemble_reference) 
{
    auto* oe = db->GetOtherEnsemble(ensemble_reference, true);
    auto res = other_ensemble_updaters.find(ensemble_reference);
    if (res == other_ensemble_updaters.end()) {
        res = other_ensemble_updaters.insert({ensemble_reference, oe}).first;
        (res->second).BindParent(this);
        all_updaters.push_back(&(res->second));
    }
    return &(res->second);
}

// Returns NULL if it couldn't find the instance
ServiceComponentUpdater* DAB_Database_Updater::GetServiceComponentUpdater_GlobalID(
    const service_component_global_id_t global_id) 
{
    auto* service_component = db->GetServiceComponent_Global(global_id);
    if (service_component == NULL) {
        return NULL;
    }

    const auto service_ref = service_component->service_reference;
    const auto component_id = service_component->component_id;

    const auto key = std::pair<service_id_t, service_component_id_t>(
        service_ref, component_id);

    auto res = service_component_updaters.find(key);
    if (res == service_component_updaters.end()) {
        res = service_component_updaters.insert({key, service_component}).first;
        (res->second).BindParent(this);
        all_updaters.push_back(&(res->second));
    }
    return &(res->second);
}

ServiceComponentUpdater* DAB_Database_Updater::GetServiceComponentUpdater_Subchannel(
    const subchannel_id_t subchannel_id) 
{
    auto* service_component = db->GetServiceComponent_Subchannel(subchannel_id);
    if (service_component == NULL) {
        return NULL;
    }

    const auto service_ref = service_component->service_reference;
    const auto component_id = service_component->component_id;

    const auto key = std::pair<service_id_t, service_component_id_t>(
        service_ref, component_id);

    auto res = service_component_updaters.find(key);
    if (res == service_component_updaters.end()) {
        res = service_component_updaters.insert({key, service_component}).first;
        (res->second).BindParent(this);
        all_updaters.push_back(&(res->second));
    }
    return &(res->second);
}

void DAB_Database_Updater::ExtractCompletedDatabase(DAB_Database& dest_db) {
    dest_db.ClearAll(); 
    if (ensemble_updater.IsComplete()) {
        dest_db.ensemble = *ensemble_updater.GetData();
    }

    for (auto& [k, v]: service_updaters) {
        if (v.IsComplete()) {
            dest_db.services.push_back(*v.GetData());
        }
    }

    for (auto& [k, v]: service_component_updaters) {
        if (v.IsComplete()) {
            dest_db.service_components.push_back(*v.GetData());
        }
    }

    for (auto& [k, v]: subchannel_updaters) {
        if (v.IsComplete()) {
            dest_db.subchannels.push_back(*v.GetData());
        }
    }

    for (auto& [k, v]: link_service_updaters) {
        if (v.IsComplete()) {
            dest_db.link_services.push_back(*v.GetData());
        }
    }

    for (auto& [k, v]: fm_service_updaters) {
        if (v.IsComplete()) {
            dest_db.fm_services.push_back(*v.GetData());
        }
    }

    for (auto& [k, v]: drm_service_updaters) {
        if (v.IsComplete()) {
            dest_db.drm_services.push_back(*v.GetData());
        }
    }

    for (auto& [k, v]: amss_service_updaters) {
        if (v.IsComplete()) {
            dest_db.amss_services.push_back(*v.GetData());
        }
    }

    for (auto& [k, v]: other_ensemble_updaters) {
        if (v.IsComplete()) {
            dest_db.other_ensembles.push_back(*v.GetData());
        }
    }

    dest_db.RegenerateLookups();
}