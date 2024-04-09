#pragma once

#include <stdint.h>
#include <memory>
#include <string>
#include <string_view>
#include "./basic_audio_controls.h"
#include "./basic_audio_params.h"
#include "./basic_msc_runner.h"
#include "dab/constants/dab_parameters.h"
#include "dab/database/dab_database_entities.h"
#include "utility/observable.h"
#include "utility/span.h"
#include "viterbi_config.h"

class MSC_Decoder;
struct MOT_Entity;
struct Basic_Slideshow;
class Basic_Slideshow_Manager;

// Shared interface for DAB+/DAB channels
class Basic_Audio_Channel: public Basic_MSC_Runner
{
protected:
    const DAB_Parameters m_params;
    const Subchannel m_subchannel;
    const AudioServiceType m_audio_service_type;
    Basic_Audio_Controls m_controls;
    // DAB data processing components
    std::string m_dynamic_label;
    std::unique_ptr<MSC_Decoder> m_msc_decoder;
    // Programme associated data
    std::unique_ptr<Basic_Slideshow_Manager> m_slideshow_manager;
    // callbacks
    Observable<BasicAudioParams, tcb::span<const uint8_t>> m_obs_audio_data;
    Observable<std::string_view> m_obs_dynamic_label;
    Observable<MOT_Entity> m_obs_MOT_entity;
public:
    explicit Basic_Audio_Channel(const DAB_Parameters& params, const Subchannel subchannel, const AudioServiceType audio_service_type);
    virtual ~Basic_Audio_Channel() override;
    virtual void Process(tcb::span<const viterbi_bit_t> msc_bits_buf) override = 0;
    AudioServiceType GetType(void) const { return m_audio_service_type; }
    auto& GetControls(void) { return m_controls; }
    std::string_view GetDynamicLabel(void) const { return m_dynamic_label; }
    auto& GetSlideshowManager(void) { return *m_slideshow_manager; }
    auto& OnAudioData(void) { return m_obs_audio_data; }
    auto& OnDynamicLabel(void) { return m_obs_dynamic_label; }
    auto& OnMOTEntity(void) { return m_obs_MOT_entity; }
};

