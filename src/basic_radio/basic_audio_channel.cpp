#include "./basic_audio_channel.h"
#include "./basic_slideshow.h"
#include "dab/database/dab_database_entities.h"
#include "dab/msc/msc_decoder.h"
#include "dab/mot/MOT_slideshow_processor.h"
#include <fmt/core.h>

#include "./basic_radio_logging.h"
#define LOG_MESSAGE(...) BASIC_RADIO_LOG_MESSAGE(fmt::format(__VA_ARGS__))
#define LOG_ERROR(...) BASIC_RADIO_LOG_ERROR(fmt::format(__VA_ARGS__))

Basic_Audio_Channel::Basic_Audio_Channel(const DAB_Parameters& params, const Subchannel subchannel, const AudioServiceType audio_service_type) 
: m_params(params), m_subchannel(subchannel), m_audio_service_type(audio_service_type) {
    m_msc_decoder = std::make_unique<MSC_Decoder>(m_subchannel);
    m_slideshow_manager = std::make_unique<Basic_Slideshow_Manager>();
}

Basic_Audio_Channel::~Basic_Audio_Channel() = default;
