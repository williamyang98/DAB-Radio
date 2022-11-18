#include "formatters.h"
#include <fmt/core.h>

#include "modules/dab/constants/subchannel_protection_tables.h"
#include "modules/dab/constants/country_table.h"
#include "modules/dab/constants/language_table.h"
#include "modules/dab/constants/programme_type_table.h"

std::string GetSubchannelProtectionLabel(Subchannel& subchannel) {
    if (subchannel.is_uep) {
        return fmt::format("UEP {}", subchannel.uep_prot_index);
    }
    const bool is_type_A = (subchannel.eep_type == EEP_Type::TYPE_A);
    const int protection_id = subchannel.eep_prot_level+1;
    return fmt::format("EEP {}-{}", protection_id, is_type_A ? 'A' : 'B');
}

uint32_t GetSubchannelBitrate(Subchannel& subchannel) {
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

const char* GetCountryString(extended_country_id_t ecc, country_id_t country_id) {
    return GetCountryName(ecc, country_id).c_str();
}

const char* GetAACDescriptionString(bool is_SBR, bool is_PS) {
    // AAC-LC
    // HE-AACv1: AAC-LC + SBR
    // HE-AACv2: AAC-LC + SBR + PS
    if (!is_SBR) {
        return "AAC-LC";
    }
    if (!is_PS) {
        return "HE-AACv1";
    }
    return "HE-AACv2";
}

const char* GetMPEGSurroundString(MPEG_Surround mpeg) {
    switch (mpeg) {
    case MPEG_Surround::SURROUND_51:
        return "MPEG Surround 5.1";
    case MPEG_Surround::SURROUND_OTHER:
        return "MPEG Surround Other";
    case MPEG_Surround::RFA:
        return "MPEG Surround RFA";
    case MPEG_Surround::NOT_USED:
    default:
        return NULL;
    }
}