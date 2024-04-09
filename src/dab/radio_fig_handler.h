#pragma once

#include <stdint.h>
#include "utility/span.h"
#include "./fic/fig_handler_interface.h"

class DAB_Database_Updater;
struct DAB_Misc_Info;

// Connects the FIG processor to the DAB database updater
class Radio_FIG_Handler: public FIG_Handler_Interface
{
private:
    DAB_Database_Updater* m_updater = nullptr;
    DAB_Misc_Info* m_misc_info = nullptr;
public:
    ~Radio_FIG_Handler() override = default;
    void SetUpdater(DAB_Database_Updater* updater) { m_updater = updater; }
    void SetMiscInfo(DAB_Misc_Info* info) { m_misc_info = info; }
public:
    // fig 0/0 - ensemble information
    void OnEnsemble_1_ID(
        const uint8_t country_id, const uint16_t ensemble_ref,
        const uint8_t change_flags, const uint8_t alarm_flag,
        const uint8_t cif_upper, const uint8_t cif_lower) override;
    // fig 0/1 - subchannel configuration
    // Short form for UEP
    void OnSubchannel_1_Short(
        const uint8_t subchannel_id, 
        const uint16_t start_address, 
        const uint8_t table_switch, const uint8_t table_index) override;
    // Long form for EEP
    void OnSubchannel_1_Long(
        const uint8_t subchannel_id, 
        const uint16_t start_address, 
        const uint8_t option, const uint8_t protection_level, 
        const uint16_t subchannel_size) override;
    // fig 0/2 - service components type
    void OnServiceComponent_1_StreamAudioType(
        const uint8_t country_id, const uint32_t service_reference, const uint8_t extended_country_code,
        const uint8_t subchannel_id, 
        const uint8_t audio_service_type, const bool is_primary) override;
    void OnServiceComponent_1_StreamDataType(
        const uint8_t country_id, const uint32_t service_reference, const uint8_t extended_country_code,
        const uint8_t subchannel_id, 
        const uint8_t data_service_type, const bool is_primary) override;
    void OnServiceComponent_1_PacketDataType(
        const uint8_t country_id, const uint32_t service_reference, const uint8_t extended_country_code,
        const uint16_t service_component_global_id, const bool is_primary) override;
    // fig 0/3 - service component packet data type
    void OnServiceComponent_2_PacketDataType(
        const uint16_t service_component_global_id, const uint8_t subchannel_id,
        const uint8_t data_service_type, 
        const uint16_t packet_address) override;
    // fig 0/4 - service component stream mode with conditional access
    void OnServiceComponent_2_StreamConditionalAccess(
        const uint8_t subchannel_id, const uint16_t CAOrg) override;
    // fig 0/5 - service component language
    // For stream mode service components
    void OnServiceComponent_3_Short_Language(
        const uint8_t subchannel_id, const uint8_t language) override;
    // For packet mode service components that have a global id
    void OnServiceComponent_3_Long_Language(
        const uint16_t service_component_global_id, 
        const uint8_t language) override;
    // fig 0/6 - Service linking information
    // This generates our LSN (linkage set number - 12bits) and a corresponding ID
    // The ID may take the form of a service id, RDS_PI (16bit) id or a DRM id (24bit)
    void OnServiceLinkage_1_LSN_Only(
        const bool is_active_link, const bool is_hard_link, const bool is_international,
        const uint16_t linkage_set_number) override;
    void OnServiceLinkage_1_ServiceID(
        const bool is_active_link, const bool is_hard_link, const bool is_international,
        const uint16_t linkage_set_number,
        const uint8_t country_id, const uint32_t service_ref, const uint8_t extended_country_code) override;
    void OnServiceLinkage_1_RDS_PI_ID(
        const bool is_active_link, const bool is_hard_link, const bool is_international,
        const uint16_t linkage_set_number,
        const uint16_t rds_pi_id, const uint8_t extended_country_code=0x00) override;
    void OnServiceLinkage_1_DRM_ID(
        const bool is_active_link, const bool is_hard_link, const bool is_international,
        const uint16_t linkage_set_number,
        const uint32_t drm_id) override;
    // fig 0/7 - Configuration information
    void OnConfigurationInformation_1(
        const uint8_t nb_services, const uint16_t reconfiguration_count) override;
    // fig 0/8 - Service component global definition
    // Links service component to their service and subchannel 
    void OnServiceComponent_4_Short_Definition(
        const uint8_t country_id, const uint32_t service_ref, const uint8_t extended_country_code,
        const uint8_t service_component_id,
        const uint8_t subchannel_id) override;
    // For packet mode service components that have a global id
    void OnServiceComponent_4_Long_Definition(
        const uint8_t country_id, const uint32_t service_ref, const uint8_t extended_country_code,
        const uint8_t service_component_id,
        const uint16_t service_component_global_id) override;
    // fig 0/9 - Ensemble country, LTO (local time offset), international table
    void OnEnsemble_2_Country(
        const uint8_t local_time_offset, const uint8_t extended_country_code, 
        const uint8_t international_table_id) override;
    void OnEnsemble_2_Service_Country(
        const uint8_t local_time_offset, const uint8_t extended_country_code, 
        const uint8_t international_table_id,
        const uint8_t service_country_id, const uint32_t service_reference, 
        const uint8_t service_extended_country_code) override;
    // fig 0/10 - Ensemble date and time
    // Long form also includes the seconds and milliseconds
    void OnDateTime_1(
        const uint32_t modified_julian_date, // days since 17/11/1858
        const uint8_t hours, const uint8_t minutes, const uint8_t seconds, const uint16_t milliseconds,
        const bool is_leap_second, const bool is_long_form) override;
    // fig 0/13 - User application information
    void OnServiceComponent_5_UserApplication(
        const uint8_t country_id, const uint32_t service_reference, const uint8_t extended_country_code,
        const uint8_t service_component_id, 
        const uint16_t app_type, 
        const uint8_t* buf, const uint8_t N) override;
    // fig 0/14 - Packet mode FEC type 
    void OnSubchannel_2_FEC(
        const uint8_t subchannel_id, const uint8_t fec_type) override;
    // fig 0/17 - Programme type
    void OnService_1_ProgrammeType(
        const uint8_t country_id, const uint32_t service_reference, const uint8_t extended_country_code,
        const uint8_t programme_type, 
        const uint8_t language_type,  const uint8_t closed_caption_type,
        const bool has_language=false, const bool has_closed_caption=false) override;
    // fig 0/21 - Alternate frequency information
    void OnFrequencyInformation_1_Ensemble(
        const uint8_t country_id, const uint16_t ensemble_reference,
        const uint32_t frequency,
        const bool is_continuous_output,
        const bool is_geographically_adjacent, 
        const bool is_transmission_mode_I) override;
    void OnFrequencyInformation_1_RDS_PI(
        const uint16_t rds_pi_id, const uint32_t frequency,
        const bool is_time_compensated) override;
    void OnFrequencyInformation_1_DRM(
        const uint32_t drm_id, const uint32_t frequency,
        const bool is_time_compensated) override;
    void OnFrequencyInformation_1_AMSS(
        const uint32_t amss_id, const uint32_t frequency,
        const bool is_time_compensated) override;
    // fig 0/24 - Other ensemble services
    void OnOtherEnsemble_1_Service(
        const uint8_t country_id, const uint32_t service_reference, const uint8_t extended_country_code,
        const uint8_t ensemble_country_id, const uint16_t ensemble_reference) override;
    // fig 1/0 - Ensemble label
    void OnEnsemble_3_Label(
        const uint8_t country_id, const uint16_t ensemble_reference,
        const uint16_t abbreviation_field,
        tcb::span<const uint8_t> buf) override;
    // fig 1/1 - Short form service identifier label
    // fig 1/5 - Long form service identifier label
    void OnService_2_Label(
        const uint8_t country_id, const uint32_t service_reference, const uint8_t extended_country_code,
        const uint16_t abbreviation_field,
        tcb::span<const uint8_t> buf) override;
    // fig 1/4 - Non-primary service component label
    void OnServiceComponent_6_Label(
        const uint8_t country_id, const uint32_t service_reference, const uint8_t extended_country_code,
        const uint8_t service_component_id,
        const uint16_t abbreviation_field,
        tcb::span<const uint8_t> buf) override;
};