#pragma once

#include <stdint.h>
#include <string>
#include <vector>
#include <memory>

#include "basic_threaded_channel.h"
#include "basic_audio_params.h"
#include "modules/dab/constants/dab_parameters.h"
#include "modules/dab/database/dab_database_entities.h"
#include "utility/observable.h"
#include "utility/span.h"
#include "viterbi_config.h"

class MSC_Decoder;
class AAC_Frame_Processor;
class AAC_Audio_Decoder;
class AAC_Data_Decoder;
struct MOT_Entity;

class Basic_Slideshow;
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
class Basic_DAB_Plus_Channel: public BasicThreadedChannel
{
private:
    const DAB_Parameters params;
    const Subchannel subchannel;
    Basic_DAB_Plus_Controls controls;
    // DAB data processing components
    std::unique_ptr<MSC_Decoder> msc_decoder;
    std::unique_ptr<AAC_Frame_Processor> aac_frame_processor;
    std::unique_ptr<AAC_Audio_Decoder> aac_audio_decoder;
    std::unique_ptr<AAC_Data_Decoder> aac_data_decoder;
    // Set buffer to operate on
    tcb::span<const viterbi_bit_t> msc_bits_buf;
    // Programme associated data
    std::string dynamic_label;
    std::unique_ptr<Basic_Slideshow_Manager> slideshow_manager;
    // callbacks
    Observable<BasicAudioParams, tcb::span<const uint8_t>> obs_audio_data;
    Observable<std::string&> obs_dynamic_label;
    Observable<Basic_Slideshow&> obs_slideshow;
    Observable<MOT_Entity&> obs_MOT_entity;
public:
    Basic_DAB_Plus_Channel(const DAB_Parameters _params, const Subchannel _subchannel);
    ~Basic_DAB_Plus_Channel();

    // We use callbacks to glue all the components together
    // this pointer is passed as a parameter on creation, and thus cannot be changed
    Basic_DAB_Plus_Channel(Basic_DAB_Plus_Channel&) = delete;
    Basic_DAB_Plus_Channel(Basic_DAB_Plus_Channel&&) = delete;
    Basic_DAB_Plus_Channel& operator=(Basic_DAB_Plus_Channel&) = delete;
    Basic_DAB_Plus_Channel& operator=(Basic_DAB_Plus_Channel&&) = delete;

    void SetBuffer(tcb::span<const viterbi_bit_t> _buf);
    auto& GetControls(void) { return controls; }
    const auto& GetDynamicLabel(void) const { return dynamic_label; }
    auto& GetSlideshowManager(void) { return *slideshow_manager; }
    auto& OnAudioData(void) { return obs_audio_data; }
    auto& OnDynamicLabel(void) { return obs_dynamic_label; }
    auto& OnSlideshow(void) { return obs_slideshow; }
    auto& OnMOTEntity(void) { return obs_MOT_entity; }
protected:
    virtual void BeforeRun();
    virtual void Run();
private:
    void SetupCallbacks(void);
};
