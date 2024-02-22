#pragma once

#include "dab/database/dab_database_entities.h"
#include "dab/audio/aac_frame_processor.h"

std::string GetSubchannelProtectionLabel(const Subchannel& subchannel);
uint32_t GetSubchannelBitrate(const Subchannel& subchannel);
const char* GetTransportModeString(const TransportMode transport_mode);
const char* GetAudioTypeString(const AudioServiceType audio_type);
const char* GetDataTypeString(const DataServiceType data_type);
const char* GetProgrammeTypeString(uint8_t inter_table_id, programme_id_t program_id);
const char* GetLanguageTypeString(language_id_t language_id);
const char* GetCountryString(extended_country_id_t ecc, country_id_t country_id);
const char* GetAACDescriptionString(bool is_SBR, bool is_PS);
const char* GetMPEGSurroundString(MPEG_Surround mpeg);
