#include "formatters.h"
#include <fmt/core.h>

#include "dab/constants/subchannel_protection_tables.h"

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
