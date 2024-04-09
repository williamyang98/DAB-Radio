#include "./basic_audio_channel.h"
#include <assert.h>
#include <memory>
#include "dab/constants/dab_parameters.h"
#include "dab/database/dab_database_entities.h"
#include "dab/msc/msc_decoder.h"
#include "./basic_slideshow.h"

Basic_Audio_Channel::Basic_Audio_Channel(const DAB_Parameters& params, const Subchannel subchannel, const AudioServiceType audio_service_type) 
: m_params(params), m_subchannel(subchannel), m_audio_service_type(audio_service_type) {
    assert(subchannel.is_complete);
    m_msc_decoder = std::make_unique<MSC_Decoder>(m_subchannel);
    m_slideshow_manager = std::make_unique<Basic_Slideshow_Manager>();
}

Basic_Audio_Channel::~Basic_Audio_Channel() = default;
