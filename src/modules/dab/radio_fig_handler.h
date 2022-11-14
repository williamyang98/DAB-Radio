#pragma once

#include "fic/fig_handler_interface.h"
#include "utility/span.h"

class DAB_Database_Updater;
struct DAB_Misc_Info;

// Connects the FIG processor to the DAB database updater
class Radio_FIG_Handler: public FIG_Handler_Interface
{
private:
    DAB_Database_Updater* updater = NULL;
    DAB_Misc_Info* misc_info = NULL;
public:
    void SetUpdater(DAB_Database_Updater* _updater) { updater = _updater; }
    void SetMiscInfo(DAB_Misc_Info* _info) { misc_info = _info; }
public:
    // fig 0/0 - ensemble information
    virtual void OnEnsemble_1_ID(
        const uint8_t country_id, const uint16_t ensemble_ref,
        const uint8_t change_flags, const uint8_t alarm_flag,
        const uint8_t cif_upper, const uint8_t cif_lower);
    // fig 0/1 - subchannel configuration
    // Short form for UEP
    virtual void OnSubchannel_1_Short(
        const uint8_t subchannel_id, 
        const uint16_t start_address, 
        const uint8_t table_switch, const uint8_t table_index);
    // Long form for EEP
    virtual void OnSubchannel_1_Long(
        const uint8_t subchannel_id, 
        const uint16_t start_address, 
        const uint8_t option, const uint8_t protection_level, 
        const uint16_t subchannel_size);
    // fig 0/2 - service components type
    virtual void OnServiceComponent_1_StreamAudioType(
        const uint8_t country_id, const uint32_t service_reference, const uint8_t extended_country_code,
        const uint8_t subchannel_id, 
        const uint8_t audio_service_type, const bool is_primary);
    virtual void OnServiceComponent_1_StreamDataType(
        const uint8_t country_id, const uint32_t service_reference, const uint8_t extended_country_code,
        const uint8_t subchannel_id, 
        const uint8_t data_service_type, const bool is_primary);
    virtual void OnServiceComponent_1_PacketDataType(
        const uint8_t country_id, const uint32_t service_reference, const uint8_t extended_country_code,
        const uint16_t service_component_global_id, const bool is_primary);
    // fig 0/3 - service component packet data type
    virtual void OnServiceComponent_2_PacketDataType(
        const uint16_t service_component_global_id, const uint8_t subchannel_id,
        const uint8_t data_service_type, 
        const uint16_t packet_address);
    // fig 0/4 - service component stream mode with conditional access
    virtual void OnServiceComponent_2_StreamConditionalAccess(
        const uint8_t subchannel_id, const uint16_t CAOrg);
    // fig 0/5 - service component language
    // For stream mode service components
    virtual void OnServiceComponent_3_Short_Language(
        const uint8_t subchannel_id, const uint8_t language);
    // For packet mode service components that have a global id
    virtual void OnServiceComponent_3_Long_Language(
        const uint16_t service_component_global_id, 
        const uint8_t language);
    // fig 0/6 - Service linking information
    // This generates our LSN (linkage set number - 12bits) and a corresponding ID
    // The ID may take the form of a service id, RDS_PI (16bit) id or a DRM id (24bit)
    virtual void OnServiceLinkage_1_LSN_Only(
        const bool is_active_link, const bool is_hard_link, const bool is_international,
        const uint16_t linkage_set_number);
    virtual void OnServiceLinkage_1_ServiceID(
        const bool is_active_link, const bool is_hard_link, const bool is_international,
        const uint16_t linkage_set_number,
        const uint8_t country_id, const uint32_t service_ref, const uint8_t extended_country_code);
    virtual void OnServiceLinkage_1_RDS_PI_ID(
        const bool is_active_link, const bool is_hard_link, const bool is_international,
        const uint16_t linkage_set_number,
        const uint16_t rds_pi_id, const uint8_t extended_country_code=0x00);
    virtual void OnServiceLinkage_1_DRM_ID(
        const bool is_active_link, const bool is_hard_link, const bool is_international,
        const uint16_t linkage_set_number,
        const uint32_t drm_id);
    // fig 0/7 - Configuration information
    virtual void OnConfigurationInformation_1(
        const uint8_t nb_services, const uint16_t reconfiguration_count);
    // fig 0/8 - Service component global definition
    // Links service component to their service and subchannel 
    virtual void OnServiceComponent_4_Short_Definition(
        const uint8_t country_id, const uint32_t service_ref, const uint8_t extended_country_code,
        const uint8_t service_component_id,
        const uint8_t subchannel_id);
    // For packet mode service components that have a global id
    virtual void OnServiceComponent_4_Long_Definition(
        const uint8_t country_id, const uint32_t service_ref, const uint8_t extended_country_code,
        const uint8_t service_component_id,
        const uint16_t service_component_global_id);
    // fig 0/9 - Ensemble country, LTO (local time offset), international table
    virtual void OnEnsemble_2_Country(
        const uint8_t local_time_offset, const uint8_t extended_country_code, 
        const uint8_t international_table_id); 
    virtual void OnEnsemble_2_Service_Country(
        const uint8_t local_time_offset, const uint8_t extended_country_code, 
        const uint8_t international_table_id,
        const uint8_t service_country_id, const uint32_t service_reference, 
        const uint8_t service_extended_country_code); 
    // fig 0/10 - Ensemble date and time
    // Long form also includes the seconds and milliseconds
    virtual void OnDateTime_1(
        const uint32_t modified_julian_date, // days since 17/11/1858
        const uint8_t hours, const uint8_t minutes, const uint8_t seconds, const uint16_t milliseconds,
        const bool is_leap_second, const bool is_long_form);
    // fig 0/13 - User application information
    virtual void OnServiceComponent_5_UserApplication(
        const uint8_t country_id, const uint32_t service_reference, const uint8_t extended_country_code,
        const uint8_t service_component_id, 
        const uint16_t app_type, 
        const uint8_t* buf, const uint8_t N);
    // fig 0/14 - Packet mode FEC type 
    virtual void OnSubchannel_2_FEC(
        const uint8_t subchannel_id, const uint8_t fec_type);
    // fig 0/17 - Programme type
    virtual void OnService_1_ProgrammeType(
        const uint8_t country_id, const uint32_t service_reference, const uint8_t extended_country_code,
        const uint8_t programme_type, 
        const uint8_t language_type,  const uint8_t closed_caption_type,
        const bool has_language=false, const bool has_closed_caption=false);
    // fig 0/21 - Alternate frequency information
    virtual void OnFrequencyInformation_1_Ensemble(
        const uint8_t country_id, const uint16_t ensemble_reference,
        const uint32_t frequency,
        const bool is_continuous_output,
        const bool is_geographically_adjacent, 
        const bool is_transmission_mode_I);
    virtual void OnFrequencyInformation_1_RDS_PI(
        const uint16_t rds_pi_id, const uint32_t frequency,
        const bool is_time_compensated);
    virtual void OnFrequencyInformation_1_DRM(
        const uint32_t drm_id, const uint32_t frequency,
        const bool is_time_compensated);
    virtual void OnFrequencyInformation_1_AMSS(
        const uint32_t amss_id, const uint32_t frequency,
        const bool is_time_compensated);
    // fig 0/24 - Other ensemble services
    virtual void OnOtherEnsemble_1_Service(
        const uint8_t country_id, const uint32_t service_reference, const uint8_t extended_country_code,
        const uint8_t ensemble_country_id, const uint16_t ensemble_reference);
    // fig 1/0 - Ensemble label
    virtual void OnEnsemble_3_Label(
        const uint8_t country_id, const uint16_t ensemble_reference,
        const uint16_t abbreviation_field,
        tcb::span<const uint8_t> buf);
    // fig 1/1 - Short form service identifier label
    // fig 1/5 - Long form service identifier label
    virtual void OnService_2_Label(
        const uint8_t country_id, const uint32_t service_reference, const uint8_t extended_country_code,
        const uint16_t abbreviation_field,
        tcb::span<const uint8_t> buf);
    // fig 1/4 - Non-primary service component label
    virtual void OnServiceComponent_6_Label(
        const uint8_t country_id, const uint32_t service_reference, const uint8_t extended_country_code,
        const uint8_t service_component_id,
        const uint16_t abbreviation_field,
        tcb::span<const uint8_t> buf);
};