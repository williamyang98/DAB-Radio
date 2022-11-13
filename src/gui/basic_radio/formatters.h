#pragma once

#include "modules/dab/database/dab_database_entities.h"

std::string GetSubchannelProtectionLabel(Subchannel& subchannel);
uint32_t GetSubchannelBitrate(Subchannel& subchannel);
const char* GetTransportModeString(const TransportMode transport_mode);
const char* GetAudioTypeString(const AudioServiceType audio_type);
const char* GetDataTypeString(const DataServiceType data_type);
