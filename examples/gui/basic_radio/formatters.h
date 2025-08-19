#pragma once

#include <stdint.h>
#include <string>
#include "dab/audio/aac_frame_processor.h"
#include "dab/database/dab_database_entities.h"
#include "dab/database/dab_database_types.h"

std::string GetSubchannelProtectionLabel(const Subchannel& subchannel);
uint32_t GetSubchannelBitrate(const Subchannel& subchannel);
const char* GetTransportModeString(const TransportMode transport_mode);
const char* GetAudioTypeString(const AudioServiceType audio_type);
const char* GetDataTypeString(const DataServiceType data_type);
const char* GetProgrammeTypeString(uint8_t inter_table_id, programme_id_t program_id);
const char* GetLanguageTypeString(language_id_t language_id);
const char* GetUserApplicationTypeString(const user_application_type_t application_type);
const char* GetCountryString(extended_country_id_t ecc, country_id_t country_id);
const char* GetAACDescriptionString(bool is_spectral_band_replication, bool is_parametric_stereo);
const char* GetMPEGSurroundString(MPEG_Surround mpeg);
