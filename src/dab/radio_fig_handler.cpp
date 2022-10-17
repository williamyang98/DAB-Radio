#include "radio_fig_handler.h"
#include "database/dab_database_updater.h"

#include "constants/subchannel_protection_tables.h"
#include "algorithms/modified_julian_date.h"

#include <stdio.h>

#define PRINT_LOG_MESSAGE 1
#define PRINT_LOG_ERROR 1

#if PRINT_LOG_MESSAGE
    #define LOG_MESSAGE(fmt, ...)    fprintf(stderr, "[fh] " fmt, ##__VA_ARGS__)
#else
    #define LOG_MESSAGE(...) (void)0
#endif

#if PRINT_LOG_ERROR
    #define LOG_ERROR(fmt, ...) fprintf(stderr, "ERROR: [fh] " fmt, ##__VA_ARGS__)
#else
    #define LOG_ERROR(...)   (void)0
#endif

// fig 0/0 - ensemble information
void Radio_FIG_Handler::OnEnsemble_1_ID(
    const int cif_index,
    const uint8_t country_id, const uint16_t ensemble_ref,
    const uint8_t change_flags, const uint8_t alarm_flag,
    const uint8_t cif_upper, const uint8_t cif_lower) 
{
    if (!updater) return;
    auto u = updater->GetEnsembleUpdater();    
    u->SetCountryID(country_id);
    u->SetReference(ensemble_ref);

    // TODO: how to handle synchronisation with cif upper and lower counters
}

// fig 0/1 - subchannel configuration
// Short form for UEP
void Radio_FIG_Handler::OnSubchannel_1_Short(
    const int cif_index,
    const uint8_t subchannel_id, 
    const uint16_t start_address, 
    const uint8_t table_switch, const uint8_t table_index) 
{
    if (!updater) return;
    auto u = updater->GetSubchannelUpdater(subchannel_id);
    u->SetStartAddress(start_address);
    u->SetIsUEP(true);

    // reserved for future tables
    if (table_switch) {
        LOG_ERROR("Received an unsupported table switch for UEP (%u)\n", 
            table_switch);
        return;
    } 

    if (table_index >= UEP_PROTECTION_TABLE_SIZE) {
        LOG_ERROR("Received an index outside of table for UEP (%u/%u)\n", 
            table_index, UEP_PROTECTION_TABLE_SIZE);
        return;
    }

    const auto props = UEP_PROTECTION_TABLE[table_index];
    u->SetUEPProtIndex(table_index);
    u->SetLength(props.subchannel_size);
}

// Long form for EEP
void Radio_FIG_Handler::OnSubchannel_1_Long(
    const int cif_index,
    const uint8_t subchannel_id, 
    const uint16_t start_address, 
    const uint8_t option, const uint8_t protection_level, 
    const uint16_t subchannel_size)
{
    if (!updater) return;
    auto u = updater->GetSubchannelUpdater(subchannel_id);
    u->SetIsUEP(false);
    u->SetStartAddress(start_address);
    u->SetEEPType(option ? EEP_Type::TYPE_B : EEP_Type::TYPE_A);
    u->SetEEPProtLevel(protection_level);
    u->SetLength(subchannel_size);
}

// fig 0/2 - service components type
void Radio_FIG_Handler::OnServiceComponent_1_StreamAudioType(
    const int cif_index,
    const uint8_t country_id, const uint32_t service_reference, const uint8_t extended_country_code,
    const uint8_t subchannel_id, 
    const uint8_t audio_service_type, const bool is_primary)
{
    if (!updater) return;
    ServiceUpdater* s_u = NULL;
    ServiceComponentUpdater* sc_u = NULL;

    // get service
    s_u = updater->GetServiceUpdater(service_reference);
    s_u->SetCountryID(country_id);
    s_u->SetExtendedCountryCode(extended_country_code);

    // attempt to find the corresponding service component
    if (is_primary) {
        sc_u = updater->GetServiceComponentUpdater_Service(service_reference, 0);
    } else {
        sc_u = updater->GetServiceComponentUpdater_Subchannel(subchannel_id);
    } 
    if (!sc_u) {
        return;
    }

    sc_u->SetSubchannel(subchannel_id);
    sc_u->SetTransportMode(TransportMode::STREAM_MODE_AUDIO);

    switch (audio_service_type) {
    case 0 : 
        sc_u->SetAudioServiceType(AudioServiceType::DAB); 
        break;
    case 63: 
        sc_u->SetAudioServiceType(AudioServiceType::DAB_PLUS); 
        break;
    default:
        LOG_ERROR("Unknown audio service type %u\n", audio_service_type);
        break;
    }

}

void Radio_FIG_Handler::OnServiceComponent_1_StreamDataType(
    const int cif_index,
    const uint8_t country_id, const uint32_t service_reference, const uint8_t extended_country_code,
    const uint8_t subchannel_id, 
    const uint8_t data_service_type, const bool is_primary)
{
    if (!updater) return;
    ServiceUpdater* s_u = NULL;
    ServiceComponentUpdater* sc_u = NULL;

    // get service
    s_u = updater->GetServiceUpdater(service_reference);
    s_u->SetCountryID(country_id);
    s_u->SetExtendedCountryCode(extended_country_code);

    // attempt to find the corresponding service component
    if (is_primary) {
        sc_u = updater->GetServiceComponentUpdater_Service(service_reference, 0);
    } else {
        sc_u = updater->GetServiceComponentUpdater_Subchannel(subchannel_id);
    }
    if (!sc_u) {
        return;
    }

    sc_u->SetSubchannel(subchannel_id);
    sc_u->SetTransportMode(TransportMode::STREAM_MODE_DATA);

    switch (data_service_type) {
    case 5:
        sc_u->SetDataServiceType(DataServiceType::TRANSPARENT_CHANNEL);
        break;
    case 24:
        sc_u->SetDataServiceType(DataServiceType::MPEG2);
        break;
    case 60:
        sc_u->SetDataServiceType(DataServiceType::MOT);
        break;
    case 63:
        sc_u->SetDataServiceType(DataServiceType::PROPRIETARY);
        break;
    default:
        LOG_ERROR("Unsupported data service type %u\n", data_service_type);
        break;
    }    
}

void Radio_FIG_Handler::OnServiceComponent_1_PacketDataType(
    const int cif_index,
    const uint8_t country_id, const uint32_t service_reference, const uint8_t extended_country_code,
    const uint16_t service_component_global_id, const bool is_primary)
{
    if (!updater) return;
    ServiceUpdater* s_u = NULL;
    ServiceComponentUpdater* sc_u = NULL;

    // get service
    s_u = updater->GetServiceUpdater(service_reference);
    s_u->SetCountryID(country_id);
    s_u->SetExtendedCountryCode(extended_country_code);

    // attempt to find the corresponding service component
    if (is_primary) {
        sc_u = updater->GetServiceComponentUpdater_Service(service_reference, 0);
    } else {
        sc_u = updater->GetServiceComponentUpdater_GlobalID(service_component_global_id);
    }
    if (!sc_u) {
        return;
    }

    sc_u->SetTransportMode(TransportMode::PACKET_MODE_DATA);
    sc_u->SetGlobalID(service_component_global_id);
}

// fig 0/3 - service component packet data type
void Radio_FIG_Handler::OnServiceComponent_2_PacketDataType(
    const int cif_index,
    const uint16_t service_component_global_id, const uint8_t subchannel_id,
    const uint8_t data_service_type, 
    const uint16_t packet_address)
{
    if (!updater) return;
    ServiceComponentUpdater* u = NULL;
    u = updater->GetServiceComponentUpdater_Subchannel(subchannel_id);
    if (!u) {
        u = updater->GetServiceComponentUpdater_GlobalID(service_component_global_id);
    }
    if (!u) {
        return;
    }

    u->SetSubchannel(subchannel_id);
    u->SetTransportMode(TransportMode::PACKET_MODE_DATA);
    u->SetGlobalID(service_component_global_id);
    // TODO: packet address
}

// fig 0/4 - service component stream mode with conditional access
void Radio_FIG_Handler::OnServiceComponent_2_StreamConditionalAccess(
    const int cif_index,
    const uint8_t subchannel_id, const uint16_t CAOrg)
{
    // TODO: we aren't going to implement conditional access
}

// fig 0/5 - service component language
// For stream mode service components
void Radio_FIG_Handler::OnServiceComponent_3_Short_Language(
    const int cif_index,
    const uint8_t subchannel_id, const uint8_t language)
{
    if (!updater) return;
    auto sc_u = updater->GetServiceComponentUpdater_Subchannel(subchannel_id);
    if (!sc_u) {
        return;
    }

    const auto service_reference = sc_u->GetServiceReference();
    auto s_u = updater->GetServiceUpdater(service_reference);
    s_u->SetLanguage(language);
}

// For packet mode service components that have a global id
void Radio_FIG_Handler::OnServiceComponent_3_Long_Language(
    const int cif_index,
    const uint16_t service_component_global_id, 
    const uint8_t language)
{
    if (!updater) return;
    auto sc_u = updater->GetServiceComponentUpdater_GlobalID(service_component_global_id);
    if (!sc_u) {
        return;
    }
    const auto service_reference = sc_u->GetServiceReference(); 
    auto s_u = updater->GetServiceUpdater(service_reference);
    s_u->SetLanguage(language);
}

// fig 0/6 - Service linking information
// This generates our LSN (linkage set number - 12bits) and a corresponding ID
// The ID may take the form of a service id, RDS_PI (16bit) id or a DRM id (24bit)
void Radio_FIG_Handler::OnServiceLinkage_1_LSN_Only(
    const int cif_index,
    const bool is_active_link, const bool is_hard_link, const bool is_international,
    const uint16_t linkage_set_number)
{
    if (!updater) return;
    auto u = updater->GetLinkServiceUpdater(linkage_set_number);
    u->SetIsActiveLink(is_active_link);
    u->SetIsHardLink(is_hard_link);
    u->SetIsInternational(is_international);
}

void Radio_FIG_Handler::OnServiceLinkage_1_ServiceID(
    const int cif_index,
    const bool is_active_link, const bool is_hard_link, const bool is_international,
    const uint16_t linkage_set_number,
    const uint8_t country_id, const uint32_t service_ref, const uint8_t extended_country_code)
{
    if (!updater) return;
    auto l_u = updater->GetLinkServiceUpdater(linkage_set_number);
    auto s_u = updater->GetServiceUpdater(service_ref);

    l_u->SetServiceReference(service_ref);
    l_u->SetIsActiveLink(is_active_link);
    l_u->SetIsHardLink(is_hard_link);
    l_u->SetIsInternational(is_international);

    s_u->SetCountryID(country_id);
    s_u->SetExtendedCountryCode(extended_country_code);
}

void Radio_FIG_Handler::OnServiceLinkage_1_RDS_PI_ID(
    const int cif_index,
    const bool is_active_link, const bool is_hard_link, const bool is_international,
    const uint16_t linkage_set_number,
    const uint16_t rds_pi_id, const uint8_t extended_country_code)
{
    if (!updater) return;
    auto l_u = updater->GetLinkServiceUpdater(linkage_set_number);
    l_u->SetIsActiveLink(is_active_link);
    l_u->SetIsHardLink(is_hard_link);
    l_u->SetIsInternational(is_international);

    auto fm_u = updater->GetFMServiceUpdater(rds_pi_id);
    fm_u->SetLinkageSetNumber(linkage_set_number);

    const auto service_reference = l_u->GetServiceReference();
    auto s_u = updater->GetServiceUpdater(service_reference);
    s_u->SetExtendedCountryCode(extended_country_code);
}

void Radio_FIG_Handler::OnServiceLinkage_1_DRM_ID(
    const int cif_index,
    const bool is_active_link, const bool is_hard_link, const bool is_international,
    const uint16_t linkage_set_number,
    const uint32_t drm_id)
{
    if (!updater) return;
    auto l_u = updater->GetLinkServiceUpdater(linkage_set_number);
    l_u->SetIsActiveLink(is_active_link);
    l_u->SetIsHardLink(is_hard_link);
    l_u->SetIsInternational(is_international);

    auto drm_u = updater->GetDRMServiceUpdater(drm_id);
    drm_u->SetLinkageSetNumber(linkage_set_number);
}

// fig 0/7 - Configuration information
void Radio_FIG_Handler::OnConfigurationInformation_1(
    const int cif_index,
    const uint8_t nb_services, const uint16_t reconfiguration_count)
{
    if (!updater) return;
    auto u = updater->GetEnsembleUpdater();
    u->SetNumberServices(nb_services);
    u->SetReconfigurationCount(reconfiguration_count);
}

// fig 0/8 - Service component global definition
// Links service component to their service and subchannel 
void Radio_FIG_Handler::OnServiceComponent_4_Short_Definition(
    const int cif_index,
    const uint8_t country_id, const uint32_t service_ref, const uint8_t extended_country_code,
    const uint8_t service_component_id,
    const uint8_t subchannel_id)
{
    if (!updater) return;
    auto s_u = updater->GetServiceUpdater(service_ref);
    s_u->SetCountryID(country_id);
    s_u->SetExtendedCountryCode(extended_country_code);

    auto sc_u = updater->GetServiceComponentUpdater_Service(service_ref, service_component_id);
    sc_u->SetSubchannel(subchannel_id);
}

// For packet mode service components that have a global id
void Radio_FIG_Handler::OnServiceComponent_4_Long_Definition(
    const int cif_index,
    const uint8_t country_id, const uint32_t service_ref, const uint8_t extended_country_code,
    const uint8_t service_component_id,
    const uint16_t service_component_global_id)
{
    if (!updater) return;
    auto s_u = updater->GetServiceUpdater(service_ref);
    s_u->SetCountryID(country_id);
    s_u->SetExtendedCountryCode(extended_country_code);

    auto sc_u = updater->GetServiceComponentUpdater_Service(service_ref, service_component_id);
    sc_u->SetGlobalID(service_component_id);
}

// fig 0/9 - Ensemble country, LTO (local time offset), international table
void Radio_FIG_Handler::OnEnsemble_2_Country(
    const int cif_index,
    const uint8_t local_time_offset, const uint8_t extended_country_code, 
    const uint8_t international_table_id)
{
    if (!updater) return;
    auto u = updater->GetEnsembleUpdater();
    u->SetExtendedCountryCode(extended_country_code);

    // local time offset is a 6 bit field
    // b5 = 0:positive, 1:negative
    // b4:b0 = scalar value
    // LTO = (-1)^b5 * (b4:b0) * 0.5 
    // LTO is in hours
    const uint8_t sign =  (local_time_offset & 0b00100000);
    const uint8_t value = (local_time_offset & 0b00011111);
    // Value goes from -155 to 155 which is -15.5 to 15.5 hours
    const int LTO_hours = (sign ? -1 : 1) * value * 5;
    u->SetLocalTimeOffset(LTO_hours);
    u->SetInternationalTableID(international_table_id);
}
 
void Radio_FIG_Handler::OnEnsemble_2_Service_Country(
    const int cif_index,
    const uint8_t local_time_offset, const uint8_t extended_country_code, 
    const uint8_t international_table_id,
    const uint8_t service_country_id, const uint32_t service_reference, 
    const uint8_t service_extended_country_code)
{
    if (!updater) return;

    auto u = updater->GetEnsembleUpdater();
    u->SetExtendedCountryCode(extended_country_code);

    // local time offset is a 6 bit field
    // b5 = 0:positive, 1:negative
    // b4:b0 = scalar value
    // LTO = (-1)^b5 * (b4:b0) * 0.5 
    // LTO is in hours
    const uint8_t sign =  (local_time_offset & 0b00100000);
    const uint8_t value = (local_time_offset & 0b00011111);
    // Value goes from -155 to 155 which is -15.5 to 15.5 hours
    const int LTO_hours = (sign ? -1 : 1) * value * 5;
    u->SetLocalTimeOffset(LTO_hours);
    u->SetInternationalTableID(international_table_id);

    auto s_u = updater->GetServiceUpdater(service_reference);
    s_u->SetCountryID(service_country_id);
    s_u->SetExtendedCountryCode(service_extended_country_code);
}
 
// fig 0/10 - Ensemble date and time
// Long form also includes the seconds and milliseconds
void Radio_FIG_Handler::OnDateTime_1(
    const int cif_index,
    const uint32_t modified_julian_date, // days since 17/11/1858
    const uint8_t hours, const uint8_t minutes, const uint8_t seconds, const uint16_t milliseconds,
    const bool is_leap_second, const bool is_long_form)
{
    if (!updater) return;

    struct {
        int year;
        int month;
        int day;
    } time;
    mjd_to_ymd(static_cast<long>(modified_julian_date), &time.year, &time.month, &time.day);

    LOG_MESSAGE("Datetime: %02d/%02d/%04d %02u:%02u:%02u.%u\n",
        time.day, time.month, time.year,
        hours, minutes, seconds, milliseconds);

    // TODO: put this somewhere?
}

// fig 0/13 - User application information
void Radio_FIG_Handler::OnServiceComponent_5_UserApplication(
    const int cif_index,
    const uint8_t country_id, const uint32_t service_reference, const uint8_t extended_country_code,
    const uint8_t service_component_id, 
    const uint16_t app_type, 
    const uint8_t* buf, const uint8_t N)
{
    if (!updater) return;

    auto s_u = updater->GetServiceUpdater(service_reference); 
    s_u->SetCountryID(country_id);
    s_u->SetExtendedCountryCode(extended_country_code);

    auto sc_u = updater->GetServiceComponentUpdater_Service(service_reference, service_component_id);
    // TODO: how to handle this information?
    LOG_MESSAGE("service_ref=%u component_id=%u app_type=%u N=%u\n",
        service_reference, service_component_id, app_type, N);
}

// fig 0/14 - Packet mode FEC type 
void Radio_FIG_Handler::OnSubchannel_2_FEC(
    const int cif_index,
    const uint8_t subchannel_id, const uint8_t fec_type)
{
    if (!updater) return;
    auto u = updater->GetSubchannelUpdater(subchannel_id);
    u->SetFECScheme(fec_type);
}

// fig 0/17 - Programme type
void Radio_FIG_Handler::OnService_1_ProgrammeType(
    const int cif_index,
    const uint8_t country_id, const uint32_t service_reference, const uint8_t extended_country_code,
    const uint8_t programme_type, 
    const uint8_t language_type,  const uint8_t closed_caption_type,
    const bool has_language, const bool has_closed_caption)
{
    if (!updater) return;

    auto s_u = updater->GetServiceUpdater(service_reference);
    s_u->SetCountryID(country_id);
    s_u->SetExtendedCountryCode(extended_country_code);
    s_u->SetProgrammeType(programme_type);

    if (has_language) {
        s_u->SetLanguage(language_type);
    }

    if (has_closed_caption) {
        s_u->SetClosedCaption(closed_caption_type);
    }
}

// fig 0/21 - Alternate frequency information
void Radio_FIG_Handler::OnFrequencyInformation_1_Ensemble(
    const int cif_index,
    const uint8_t country_id, const uint16_t ensemble_reference,
    const uint32_t frequency,
    const bool is_continuous_output,
    const bool is_geographically_adjacent, 
    const bool is_transmission_mode_I)
{
    if (!updater) return;

    auto u = updater->GetOtherEnsemble(ensemble_reference);
    u->SetCountryID(country_id);
    u->SetIsContinuousOutput(is_continuous_output);
    u->SetIsGeographicallyAdjacent(is_geographically_adjacent);
    u->SetIsTransmissionModeI(is_transmission_mode_I);
    u->SetFrequency(frequency);
}

void Radio_FIG_Handler::OnFrequencyInformation_1_RDS_PI(
    const int cif_index,
    const uint16_t rds_pi_id, const uint32_t frequency,
    const bool is_time_compensated)
{
    if (!updater) return;

    auto u = updater->GetFMServiceUpdater(rds_pi_id);
    u->SetIsTimeCompensated(is_time_compensated);
    u->AddFrequency(frequency);
}

void Radio_FIG_Handler::OnFrequencyInformation_1_DRM(
    const int cif_index,
    const uint32_t drm_id, const uint32_t frequency,
    const bool is_time_compensated)
{
    if (!updater) return;

    auto u = updater->GetDRMServiceUpdater(drm_id);
    u->SetIsTimeCompensated(is_time_compensated); 
    u->AddFrequency(frequency);
}

void Radio_FIG_Handler::OnFrequencyInformation_1_AMSS(
    const int cif_index,
    const uint32_t amss_id, const uint32_t frequency,
    const bool is_time_compensated)
{
    if (!updater) return;

    auto u = updater->GetAMSS_ServiceUpdater(amss_id);
    u->SetIsTimeCompensated(is_time_compensated);
    u->AddFrequency(frequency);
}

// fig 0/24 - Other ensemble services
void Radio_FIG_Handler::OnOtherEnsemble_1_Service(
    const int cif_index,
    const uint8_t country_id, const uint32_t service_reference, const uint8_t extended_country_code,
    const uint8_t ensemble_country_id, const uint16_t ensemble_reference)
{
    if (!updater) return;

    auto s_u = updater->GetServiceUpdater(service_reference); 
    s_u->SetCountryID(country_id);
    s_u->SetExtendedCountryCode(extended_country_code);

    auto e_u = updater->GetOtherEnsemble(ensemble_reference);
    e_u->SetCountryID(ensemble_country_id);
}

// fig 1/0 - Ensemble label
void Radio_FIG_Handler::OnEnsemble_3_Label(
    const int cif_index,
    const uint8_t country_id, const uint16_t ensemble_reference,
    const uint16_t abbreviation_field,
    const uint8_t* buf, const int N)
{
    if (!updater) return;

    auto e_u = updater->GetEnsembleUpdater(); 
    e_u->SetCountryID(country_id);
    e_u->SetLabel(buf, N);

    // TODO: for label handler store the abbreviation field somewhere
}

// fig 1/1 - Short form service identifier label
// fig 1/5 - Long form service identifier label
void Radio_FIG_Handler::OnService_2_Label(
    const int cif_index,
    const uint8_t country_id, const uint32_t service_reference, const uint8_t extended_country_code,
    const uint16_t abbreviation_field,
    const uint8_t* buf, const int N)
{
    if (!updater) return;

    auto s_u = updater->GetServiceUpdater(service_reference);
    s_u->SetCountryID(country_id);
    s_u->SetExtendedCountryCode(extended_country_code);
    s_u->SetLabel(buf, N);
}

// fig 1/4 - Non-primary service component label
void Radio_FIG_Handler::OnServiceComponent_6_Label(
    const int cif_index,
    const uint8_t country_id, const uint32_t service_reference, const uint8_t extended_country_code,
    const uint8_t service_component_id,
    const uint16_t abbreviation_field,
    const uint8_t* buf, const int N)
{
    if (!updater) return;

    auto s_u = updater->GetServiceUpdater(service_reference);
    s_u->SetCountryID(country_id);
    s_u->SetExtendedCountryCode(extended_country_code);

    auto sc_u = updater->GetServiceComponentUpdater_Service(service_reference, service_component_id);
    sc_u->SetLabel(buf, N);
}
