#pragma once

#include <stdint.h>
#include <string>
#include <string_view>
#include <vector>
#include <memory>

#include "./basic_audio_params.h"
#include "dab/audio/aac_frame_processor.h"
#include "dab/constants/dab_parameters.h"
#include "dab/database/dab_database_entities.h"
#include "utility/observable.h"
#include "utility/span.h"
#include "viterbi_config.h"

class MSC_Decoder;
class AAC_Audio_Decoder;
class AAC_Data_Decoder;
struct MOT_Entity;

struct Basic_Slideshow;
class Basic_Slideshow_Manager;

class Basic_DAB_Plus_Controls 
{
private:
    uint8_t flags = 0;
public:
    // Is anything enabled?
    bool GetAnyEnabled(void) const;
    bool GetAllEnabled(void) const;
    void RunAll(void);
    void StopAll(void);
    // Decode AAC audio elements
    bool GetIsDecodeAudio(void) const;
    void SetIsDecodeAudio(bool);
    // Decode AAC data_stream_element
    bool GetIsDecodeData(void) const;
    void SetIsDecodeData(bool);
    // Play audio data through sound device
    bool GetIsPlayAudio(void) const;
    void SetIsPlayAudio(bool);
private:
    void SetFlag(const uint8_t flag, const bool state);
};

// Audio channel player for DAB+
class Basic_DAB_Plus_Channel
{
private:
    const DAB_Parameters m_params;
    const Subchannel m_subchannel;
    Basic_DAB_Plus_Controls m_controls;
    // DAB data processing components
    std::unique_ptr<MSC_Decoder> m_msc_decoder;
    std::unique_ptr<AAC_Frame_Processor> m_aac_frame_processor;
    std::unique_ptr<AAC_Audio_Decoder> m_aac_audio_decoder;
    std::unique_ptr<AAC_Data_Decoder> m_aac_data_decoder;
    // Programme associated data
    std::string m_dynamic_label;
    std::unique_ptr<Basic_Slideshow_Manager> m_slideshow_manager;
    // callbacks
    Observable<BasicAudioParams, tcb::span<const uint8_t>> m_obs_audio_data;
    Observable<std::string_view> m_obs_dynamic_label;
    Observable<MOT_Entity> m_obs_MOT_entity;
    // decoder values
    SuperFrameHeader m_super_frame_header;
    bool m_is_firecode_error = false;
    bool m_is_rs_error = false;
    bool m_is_au_error = false;
    bool m_is_codec_error = false;
public:
    Basic_DAB_Plus_Channel(const DAB_Parameters& _params, const Subchannel _subchannel);
    ~Basic_DAB_Plus_Channel();
    void Process(tcb::span<const viterbi_bit_t> msc_bits_buf);
    auto& GetControls(void) { return m_controls; }
    const auto& GetDynamicLabel(void) const { return m_dynamic_label; }
    auto& GetSlideshowManager(void) { return *m_slideshow_manager; }
    auto& OnAudioData(void) { return m_obs_audio_data; }
    auto& OnDynamicLabel(void) { return m_obs_dynamic_label; }
    auto& OnMOTEntity(void) { return m_obs_MOT_entity; }
    const auto& GetSuperFrameHeader() const { return m_super_frame_header; }
    bool IsFirecodeError() const { return m_is_firecode_error; }
    bool IsRSError() const { return m_is_rs_error; }
    bool IsAUError() const { return m_is_au_error; }
    bool IsCodecError() const { return m_is_codec_error; }
private:
    void SetupCallbacks(void);
};
