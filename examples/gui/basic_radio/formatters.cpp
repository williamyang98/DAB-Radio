#include "./formatters.h"
#include <stdint.h>
#include <string>
#include <fmt/format.h>
#include "dab/audio/aac_frame_processor.h"
#include "dab/constants/country_table.h"
#include "dab/constants/language_table.h"
#include "dab/constants/programme_type_table.h"
#include "dab/constants/subchannel_protection_tables.h"
#include "dab/database/dab_database_entities.h"
#include "dab/database/dab_database_types.h"

std::string GetSubchannelProtectionLabel(const Subchannel& subchannel) {
    if (subchannel.is_uep) {
        return fmt::format("UEP {}", subchannel.uep_prot_index);
    }
    const bool is_type_A = (subchannel.eep_type == EEP_Type::TYPE_A);
    const int protection_id = subchannel.eep_prot_level+1;
    return fmt::format("EEP {}-{}", protection_id, is_type_A ? 'A' : 'B');
}

uint32_t GetSubchannelBitrate(const Subchannel& subchannel) {
    if (subchannel.is_uep) {
        const auto descriptor = GetUEPDescriptor(subchannel);
        return descriptor.bitrate;
    }
    return CalculateEEPBitrate(subchannel);
}

const char* GetTransportModeString(const TransportMode transport_mode) {
    switch (transport_mode) {
    case TransportMode::STREAM_MODE_AUDIO:
        return "Stream Audio";
    case TransportMode::STREAM_MODE_DATA:
        return "Stream Data";
    case TransportMode::PACKET_MODE_DATA:
        return "Packet Data";
    default:
        return "Unknown";
    }
}

const char* GetAudioTypeString(const AudioServiceType audio_type) {
    switch (audio_type) {
    case AudioServiceType::DAB:
        return "DAB";
    case AudioServiceType::DAB_PLUS:
        return "DAB+";
    default:
        return "Unknown";
    }
}

const char* GetDataTypeString(const DataServiceType data_type) {
    switch (data_type) {
    case DataServiceType::MOT:
        return "Multimedia Object Type";
    case DataServiceType::MPEG2:
        return "MPEG-II";
    case DataServiceType::TRANSPARENT_CHANNEL:
        return "Transparent";
    case DataServiceType::PROPRIETARY:
        return "Proprietary";
    default:
        return "Unknown";
    }
}

const char* GetProgrammeTypeString(uint8_t inter_table_id, programme_id_t program_id) {
    return GetProgrammeTypeName(inter_table_id, program_id).long_label.c_str();
}

const char* GetLanguageTypeString(language_id_t language_id) {
    return GetLanguageName(language_id).c_str();
}

const char* GetUserApplicationTypeString(const user_application_type_t application_type)
{
    switch (static_cast<UserApplicationType>(application_type)) {
    case UserApplicationType::SLIDESHOW:
        return "SlideShow";
    case UserApplicationType::TPEG:
        return "TPEG";
    case UserApplicationType::SPI:
        return "SPI";
    case UserApplicationType::DMB:
        return "DMB";
    case UserApplicationType::FILE_CASTING:
        return "Filecasting";
    case UserApplicationType::FIS:
        return "FIS";
    case UserApplicationType::JOURNALINE:
        return "Journaline®";
    default:
        return "Unknown";
    }
}

const char* GetCountryString(extended_country_id_t ecc, country_id_t country_id) {
    return GetCountryName(ecc, country_id).c_str();
}

const char* GetAACDescriptionString(bool is_spectral_band_replication, bool is_parametric_stereo) {
    // AAC-LC
    // HE-AACv1: AAC-LC + SBR
    // HE-AACv2: AAC-LC + SBR + PS
    if (!is_spectral_band_replication) return "AAC-LC";
    if (!is_parametric_stereo) return "HE-AACv1";
    return "HE-AACv2";
}

const char* GetMPEGSurroundString(MPEG_Surround mpeg) {
    switch (mpeg) {
    case MPEG_Surround::SURROUND_51:
        return "MPEG Surround 5.1";
    case MPEG_Surround::SURROUND_71:
        return "MPEG Surround 7.1";
    case MPEG_Surround::SURROUND_OTHER:
        return "MPEG Surround Other";
    case MPEG_Surround::RFA:
        return "MPEG Surround RFA";
    case MPEG_Surround::NOT_USED:
        return "";
    default:
        return "Unknown";
    }
}