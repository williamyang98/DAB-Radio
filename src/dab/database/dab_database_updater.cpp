#include "./dab_database_updater.h"
#include <stdint.h>
#include <memory>
#include <string_view>
#include <vector>
#include "utility/span.h"
#include "./dab_database.h"
#include "./dab_database_entities.h"
#include "./dab_database_types.h"

template <typename T>
bool insert_if_unique(std::vector<T>& vec, T value) {
    for (const auto& v: vec) {
        if (v == value) return false;
    }
    vec.push_back(value);
    return true;
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
    return UpdateField(GetData().reference, reference, ENSEMBLE_FLAG_REFERENCE);
}

UpdateResult EnsembleUpdater::SetCountryID(const country_id_t country_id) {
    return UpdateField(GetData().country_id, country_id, ENSEMBLE_FLAG_COUNTRY_ID);
}

UpdateResult EnsembleUpdater::SetExtendedCountryCode(const extended_country_id_t extended_country_code) {
    // 0x00 is a NULL extended country code
    // this occurs if the packet doesn't define it
    if (extended_country_code == 0x00) {
        return UpdateResult::NO_CHANGE;
    }
    return UpdateField(GetData().extended_country_code, extended_country_code, ENSEMBLE_FLAG_ECC);
}

UpdateResult EnsembleUpdater::SetLabel(tcb::span<const uint8_t> buf) {
    auto new_label = std::string_view(reinterpret_cast<const char*>(buf.data()), buf.size());
    return UpdateField(GetData().label, new_label, ENSEMBLE_FLAG_LABEL);
}

UpdateResult EnsembleUpdater::SetNumberServices(const uint8_t nb_services) {
    return UpdateField(GetData().nb_services, nb_services, ENSEMBLE_FLAG_NB_SERVICES);
}

UpdateResult EnsembleUpdater::SetReconfigurationCount(const uint16_t reconfiguration_count) {
    return UpdateField(GetData().reconfiguration_count, reconfiguration_count, ENSEMBLE_FLAG_RCOUNT);
}

UpdateResult EnsembleUpdater::SetLocalTimeOffset(const int8_t local_time_offset) {
    return UpdateField(GetData().local_time_offset, local_time_offset, ENSEMBLE_FLAG_LTO);
}

UpdateResult EnsembleUpdater::SetInternationalTableID(const uint8_t international_table_id) {
    return UpdateField(GetData().international_table_id, international_table_id, ENSEMBLE_FLAG_INTER_TABLE);
}

bool EnsembleUpdater::IsComplete() {
    return GetData().is_complete = ((m_dirty_field & ENSEMBLE_FLAG_REQUIRED) == ENSEMBLE_FLAG_REQUIRED);
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
    return UpdateField(GetData().country_id, country_id, SERVICE_FLAG_COUNTRY_ID);
}

UpdateResult ServiceUpdater::SetExtendedCountryCode(const extended_country_id_t extended_country_code) {
    if (extended_country_code == 0x00) {
        return UpdateResult::NO_CHANGE;
    }
    return UpdateField(GetData().extended_country_code, extended_country_code, SERVICE_FLAG_ECC);
}

UpdateResult ServiceUpdater::SetLabel(tcb::span<const uint8_t> buf) {
    auto new_label = std::string_view(reinterpret_cast<const char*>(buf.data()), buf.size());
    return UpdateField(GetData().label, new_label, SERVICE_FLAG_LABEL);
}

UpdateResult ServiceUpdater::SetProgrammeType(const programme_id_t programme_type) {
    return UpdateField(GetData().programme_type, programme_type, SERVICE_FLAG_PROGRAM_TYPE);
}

UpdateResult ServiceUpdater::SetLanguage(const language_id_t language) {
    return UpdateField(GetData().language, language, SERVICE_FLAG_LANGUAGE);
}

UpdateResult ServiceUpdater::SetClosedCaption(const closed_caption_id_t closed_caption) {
    return UpdateField(GetData().closed_caption, closed_caption, SERVICE_FLAG_CLOSED_CAP);
}

bool ServiceUpdater::IsComplete() {
    return GetData().is_complete = ((m_dirty_field & SERVICE_FLAG_REQUIRED) == SERVICE_FLAG_REQUIRED);
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
const uint8_t SERVICE_COMPONENT_FLAG_REQUIRED_DATA  = 0b01001000;

UpdateResult ServiceComponentUpdater::SetLabel(tcb::span<const uint8_t> buf) {
    auto new_label = std::string_view(reinterpret_cast<const char*>(buf.data()), buf.size());
    return UpdateField(GetData().label, new_label, SERVICE_COMPONENT_FLAG_LABEL);
}

UpdateResult ServiceComponentUpdater::SetTransportMode(const TransportMode transport_mode) {
    if ((m_dirty_field & SERVICE_COMPONENT_FLAG_DATA_TYPE) && (transport_mode == TransportMode::STREAM_MODE_AUDIO)) {
        OnConflict();
        return UpdateResult::CONFLICT;
    }

    return UpdateField(GetData().transport_mode, transport_mode, SERVICE_COMPONENT_FLAG_TRANSPORT_MODE);
}

UpdateResult ServiceComponentUpdater::SetAudioServiceType(const AudioServiceType audio_service_type) {
    const auto res = SetTransportMode(TransportMode::STREAM_MODE_AUDIO);
    if (res == UpdateResult::CONFLICT) {
        OnConflict();
        return UpdateResult::CONFLICT;
    }
    if (m_dirty_field & SERVICE_COMPONENT_FLAG_DATA_TYPE) {
        OnConflict();
        return UpdateResult::CONFLICT;
    }
    return UpdateField(GetData().audio_service_type, audio_service_type, SERVICE_COMPONENT_FLAG_AUDIO_TYPE);
}

UpdateResult ServiceComponentUpdater::SetDataServiceType(const DataServiceType data_service_type) {
    if (m_dirty_field & SERVICE_COMPONENT_FLAG_AUDIO_TYPE) {
        OnConflict();
        return UpdateResult::CONFLICT;
    }
    return UpdateField(GetData().data_service_type, data_service_type, SERVICE_COMPONENT_FLAG_DATA_TYPE);
}

UpdateResult ServiceComponentUpdater::SetSubchannel(const subchannel_id_t subchannel_id) {
    return UpdateField(GetData().subchannel_id, subchannel_id, SERVICE_COMPONENT_FLAG_SUBCHANNEL);
}

UpdateResult ServiceComponentUpdater::SetGlobalID(const service_component_global_id_t global_id) {
    // In some transmitters they keep changing this for some reason?
    return UpdateField(GetData().global_id, global_id, SERVICE_COMPONENT_FLAG_GLOBAL_ID, true);
}

uint32_t ServiceComponentUpdater::GetServiceReference() {
    return GetData().service_reference;
}

bool ServiceComponentUpdater::IsComplete() {
    const bool audio_complete = (m_dirty_field & SERVICE_COMPONENT_FLAG_REQUIRED_AUDIO) == SERVICE_COMPONENT_FLAG_REQUIRED_AUDIO;
    const bool data_complete  = (m_dirty_field & SERVICE_COMPONENT_FLAG_REQUIRED_DATA) == SERVICE_COMPONENT_FLAG_REQUIRED_DATA;
    const bool is_complete = (GetData().transport_mode == TransportMode::STREAM_MODE_AUDIO) ? audio_complete : data_complete;
    GetData().is_complete = is_complete;
    return is_complete;
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
    return UpdateField(GetData().start_address, start_address, SUBCHANNEL_FLAG_START_ADDRESS);
}

UpdateResult SubchannelUpdater::SetLength(const subchannel_size_t length) {
    return UpdateField(GetData().length, length, SUBCHANNEL_FLAG_LENGTH); 
}

UpdateResult SubchannelUpdater::SetIsUEP(const bool is_uep) {
    return UpdateField(GetData().is_uep, is_uep, SUBCHANNEL_FLAG_IS_UEP);
}

UpdateResult SubchannelUpdater::SetUEPProtIndex(const uep_protection_index_t uep_prot_index) {
    const auto res = SetIsUEP(true);
    if (res == UpdateResult::CONFLICT) {
        return UpdateResult::CONFLICT;
    }
    return UpdateField(GetData().uep_prot_index, uep_prot_index, SUBCHANNEL_FLAG_UEP_PROT_INDEX);
}

UpdateResult SubchannelUpdater::SetEEPProtLevel(const eep_protection_level_t eep_prot_level) {
    const auto res = SetIsUEP(false);
    if (res == UpdateResult::CONFLICT) {
        return UpdateResult::CONFLICT;
    }
    return UpdateField(GetData().eep_prot_level, eep_prot_level, SUBCHANNEL_FLAG_EEP_PROT_LEVEL);
}

UpdateResult SubchannelUpdater::SetEEPType(const EEP_Type eep_type) {
    const auto res = SetIsUEP(false);
    if (res == UpdateResult::CONFLICT) {
        return UpdateResult::CONFLICT;
    }
    return UpdateField(GetData().eep_type, eep_type, SUBCHANNEL_FLAG_EEP_TYPE);
}

UpdateResult SubchannelUpdater::SetFECScheme(const FEC_Scheme fec_scheme) {
    return UpdateField(GetData().fec_scheme, fec_scheme, SUBCHANNEL_FLAG_FEC_SCHEME);
}

bool SubchannelUpdater::IsComplete() {
    const bool eep_complete = (m_dirty_field & SUBCHANNEL_FLAG_REQUIRED_EEP) == SUBCHANNEL_FLAG_REQUIRED_EEP;
    const bool uep_complete = (m_dirty_field & SUBCHANNEL_FLAG_REQUIRED_UEP) == SUBCHANNEL_FLAG_REQUIRED_UEP;
    const bool is_complete = GetData().is_uep ? uep_complete : eep_complete;
    GetData().is_complete = is_complete;
    return is_complete;
}

// link service form
const uint8_t LINK_FLAG_ACTIVE          = 0b10000000;
const uint8_t LINK_FLAG_HARD            = 0b01000000;
const uint8_t LINK_FLAG_INTERNATIONAL   = 0b00100000;
const uint8_t LINK_FLAG_SERVICE_REF     = 0b00010000;
const uint8_t LINK_FLAG_REQUIRED        = 0b00010000;

UpdateResult LinkServiceUpdater::SetIsActiveLink(const bool is_active_link) {
    return UpdateField(GetData().is_active_link, is_active_link, LINK_FLAG_ACTIVE);
}

UpdateResult LinkServiceUpdater::SetIsHardLink(const bool is_hard_link) {
    return UpdateField(GetData().is_hard_link, is_hard_link, LINK_FLAG_HARD);
}

UpdateResult LinkServiceUpdater::SetIsInternational(const bool is_international) {
    return UpdateField(GetData().is_international, is_international, LINK_FLAG_INTERNATIONAL);
}

UpdateResult LinkServiceUpdater::SetServiceReference(const service_id_t service_reference) {
    return UpdateField(GetData().service_reference, service_reference, LINK_FLAG_SERVICE_REF);
}

service_id_t LinkServiceUpdater::GetServiceReference() {
    return GetData().service_reference;
}

bool LinkServiceUpdater::IsComplete() {
    return GetData().is_complete = ((m_dirty_field & LINK_FLAG_REQUIRED) == LINK_FLAG_REQUIRED);
}

// fm service form
const uint8_t FM_FLAG_LSN       = 0b10000000;
const uint8_t FM_FLAG_TIME_COMP = 0b01000000;
const uint8_t FM_FLAG_FREQ      = 0b00100000;
const uint8_t FM_FLAG_REQUIRED  = 0b10100000;

UpdateResult FM_ServiceUpdater::SetLinkageSetNumber(const lsn_t linkage_set_number) {
    return UpdateField(GetData().linkage_set_number, linkage_set_number, FM_FLAG_LSN);
}

UpdateResult FM_ServiceUpdater::SetIsTimeCompensated(const bool is_time_compensated) {
    return UpdateField(GetData().is_time_compensated, is_time_compensated, FM_FLAG_TIME_COMP);
}

UpdateResult FM_ServiceUpdater::AddFrequency(const freq_t frequency) {
    if (!insert_if_unique(GetData().frequencies, frequency)) return UpdateResult::NO_CHANGE;
    m_dirty_field |= FM_FLAG_FREQ;
    OnComplete();
    OnUpdate();
    return UpdateResult::SUCCESS;
}

bool FM_ServiceUpdater::IsComplete() {
    return GetData().is_complete = ((m_dirty_field & FM_FLAG_REQUIRED) == FM_FLAG_REQUIRED);
}

// drm service form
const uint8_t DRM_FLAG_LSN       = 0b10000000;
const uint8_t DRM_FLAG_TIME_COMP = 0b01000000;
const uint8_t DRM_FLAG_FREQ      = 0b00100000;
const uint8_t DRM_FLAG_REQUIRED  = 0b10100000;

UpdateResult DRM_ServiceUpdater::SetLinkageSetNumber(const lsn_t linkage_set_number) {
    return UpdateField(GetData().linkage_set_number, linkage_set_number, DRM_FLAG_LSN);
}

UpdateResult DRM_ServiceUpdater::SetIsTimeCompensated(const bool is_time_compensated) {
    return UpdateField(GetData().is_time_compensated, is_time_compensated, DRM_FLAG_TIME_COMP);
}

UpdateResult DRM_ServiceUpdater::AddFrequency(const freq_t frequency) {
    if (!insert_if_unique(GetData().frequencies, frequency)) return UpdateResult::NO_CHANGE;
    m_dirty_field |= DRM_FLAG_FREQ;
    OnComplete();
    OnUpdate();
    return UpdateResult::SUCCESS;
}

bool DRM_ServiceUpdater::IsComplete() {
    return GetData().is_complete = ((m_dirty_field & DRM_FLAG_REQUIRED) == DRM_FLAG_REQUIRED);
}

// amss service form
const uint8_t AMSS_FLAG_TIME_COMP = 0b10000000;
const uint8_t AMSS_FLAG_FREQ      = 0b01000000;
const uint8_t AMSS_FLAG_REQUIRED  = 0b01000000;

UpdateResult AMSS_ServiceUpdater::SetIsTimeCompensated(const bool is_time_compensated) {
    return UpdateField(GetData().is_time_compensated, is_time_compensated, AMSS_FLAG_TIME_COMP);
}

UpdateResult AMSS_ServiceUpdater::AddFrequency(const freq_t frequency) {
    if (!insert_if_unique(GetData().frequencies, frequency)) return UpdateResult::NO_CHANGE;
    m_dirty_field |= AMSS_FLAG_FREQ;
    OnComplete();
    OnUpdate();
    return UpdateResult::SUCCESS;
}

bool AMSS_ServiceUpdater::IsComplete() {
    return GetData().is_complete = ((m_dirty_field & AMSS_FLAG_REQUIRED) == AMSS_FLAG_REQUIRED);
}

// other ensemble form
const uint8_t OE_FLAG_COUNTRY_ID = 0b10000000;
const uint8_t OE_FLAG_CONT_OUT   = 0b01000000;
const uint8_t OE_FLAG_GEO_ADJ    = 0b00100000;
const uint8_t OE_FLAG_MODE_I     = 0b00010000;
const uint8_t OE_FLAG_FREQ       = 0b00001000;
const uint8_t OE_FLAG_REQUIRED   = 0b00001000;

UpdateResult OtherEnsembleUpdater::SetCountryID(const country_id_t country_id) {
    return UpdateField(GetData().country_id, country_id, OE_FLAG_COUNTRY_ID);
}

UpdateResult OtherEnsembleUpdater::SetIsContinuousOutput(const bool is_continuous_output) {
    return UpdateField(GetData().is_continuous_output, is_continuous_output, OE_FLAG_CONT_OUT);
}

UpdateResult OtherEnsembleUpdater::SetIsGeographicallyAdjacent(const bool is_geographically_adjacent) {
    return UpdateField(GetData().is_geographically_adjacent, is_geographically_adjacent, OE_FLAG_GEO_ADJ);
}

UpdateResult OtherEnsembleUpdater::SetIsTransmissionModeI(const bool is_transmission_mode_I) {
    return UpdateField(GetData().is_transmission_mode_I, is_transmission_mode_I, OE_FLAG_MODE_I);
}

UpdateResult OtherEnsembleUpdater::SetFrequency(const freq_t frequency) {
    return UpdateField(GetData().frequency, frequency, OE_FLAG_FREQ);
}

bool OtherEnsembleUpdater::IsComplete() {
    return GetData().is_complete = ((m_dirty_field & OE_FLAG_REQUIRED) == OE_FLAG_REQUIRED);
}

// updater parent
DAB_Database_Updater::DAB_Database_Updater() {
    m_db = std::make_unique<DAB_Database>();
    m_stats = std::make_unique<DatabaseUpdaterGlobalStatistics>();
    m_ensemble_updater = std::make_unique<EnsembleUpdater>(*(m_db.get()), *(m_stats.get()));
}

ServiceUpdater& DAB_Database_Updater::GetServiceUpdater(const service_id_t service_ref) {
    return find_or_insert_updater(
        m_db->services, m_service_updaters,
        [service_ref](const auto& e) { 
            return e.reference == service_ref; 
        },
        service_ref
    );
}

ServiceComponentUpdater& DAB_Database_Updater::GetServiceComponentUpdater_Service(
    const service_id_t service_ref, const service_component_id_t component_id) 
{
    return find_or_insert_updater(
        m_db->service_components, m_service_component_updaters,
        [service_ref, component_id](const auto& e) { 
            return (e.service_reference == service_ref) && (e.component_id == component_id); 
        },
        service_ref, component_id
    );
}

SubchannelUpdater& DAB_Database_Updater::GetSubchannelUpdater(const subchannel_id_t subchannel_id) {
    return find_or_insert_updater(
        m_db->subchannels, m_subchannel_updaters,
        [subchannel_id](const auto& e) {
            return e.id == subchannel_id;
        },
        subchannel_id
    );
}

LinkServiceUpdater& DAB_Database_Updater::GetLinkServiceUpdater(const lsn_t link_service_number) {
    return find_or_insert_updater(
        m_db->link_services, m_link_service_updaters,
        [link_service_number](const auto& e) {
            return e.id == link_service_number;
        },
        link_service_number
    );
}

FM_ServiceUpdater& DAB_Database_Updater::GetFMServiceUpdater(const fm_id_t RDS_PI_code) {
    return find_or_insert_updater(
        m_db->fm_services, m_fm_service_updaters,
        [RDS_PI_code](const auto& e) {
            return e.RDS_PI_code == RDS_PI_code;
        },
        RDS_PI_code
    );
}

DRM_ServiceUpdater& DAB_Database_Updater::GetDRMServiceUpdater(const drm_id_t drm_code) {
    return find_or_insert_updater(
        m_db->drm_services, m_drm_service_updaters,
        [drm_code](const auto& e) {
            return e.drm_code == drm_code;
        },
        drm_code
    );
}

AMSS_ServiceUpdater& DAB_Database_Updater::GetAMSS_ServiceUpdater(const amss_id_t amss_code) {
    return find_or_insert_updater(
        m_db->amss_services, m_amss_service_updaters,
        [amss_code](const auto& e) {
            return e.amss_code == amss_code;
        },
        amss_code
    );
}

OtherEnsembleUpdater& DAB_Database_Updater::GetOtherEnsemble(const ensemble_id_t ensemble_reference) {
    return find_or_insert_updater(
        m_db->other_ensembles, m_other_ensemble_updaters,
        [ensemble_reference](const auto& e) {
            return e.reference == ensemble_reference;
        },
        ensemble_reference
    );
}

ServiceComponentUpdater* DAB_Database_Updater::GetServiceComponentUpdater_GlobalID(
    const service_component_global_id_t global_id) 
{
    return find_updater(
        m_db->service_components, m_service_component_updaters,
        [global_id](const auto& e) {
            return e.global_id == global_id;
        }
    );
}

ServiceComponentUpdater* DAB_Database_Updater::GetServiceComponentUpdater_Subchannel(
    const subchannel_id_t subchannel_id) 
{
    return find_updater(
        m_db->service_components, m_service_component_updaters,
        [subchannel_id](const auto& e) {
            return e.subchannel_id == subchannel_id;
        }
    );
}