#pragma once

#include <string>
#include <stdint.h>

#include "basic_threaded_channel.h"
#include "basic_slideshow.h"
#include "basic_audio_params.h"

#include "dab/constants/dab_parameters.h"
#include "dab/database/dab_database_entities.h"

#include "../observable.h"
#include "../viterbi_config.h"

class MSC_Decoder;
class AAC_Frame_Processor;
class AAC_Audio_Decoder;
class AAC_Data_Decoder;
struct MOT_Entity;

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

    MSC_Decoder* msc_decoder;
    AAC_Frame_Processor* aac_frame_processor;
    AAC_Audio_Decoder* aac_audio_decoder;
    AAC_Data_Decoder* aac_data_decoder;

    const viterbi_bit_t* msc_bits_buf = NULL;
    int nb_msc_bits = 0;

    // Programme associated data
    std::string dynamic_label;
    Basic_Slideshow_Manager* slideshow_manager;

    // callbacks
    Observable<BasicAudioParams, const uint8_t*, const int> obs_audio_data;
    Observable<std::string&> obs_dynamic_label;
    Observable<mot_transport_id_t, Basic_Slideshow*> obs_slideshow;
    Observable<MOT_Entity*> obs_MOT_entity;
public:
    Basic_DAB_Plus_Channel(const DAB_Parameters _params, const Subchannel _subchannel);
    ~Basic_DAB_Plus_Channel();
    void SetBuffer(const viterbi_bit_t* _buf, const int _N);
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
