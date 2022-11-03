#pragma once

#include <string>

#include "basic_threaded_channel.h"
#include "dab/algorithms/viterbi_decoder.h"
#include "dab/constants/dab_parameters.h"
#include "dab/database/dab_database_entities.h"

class MSC_Decoder;
class AAC_Frame_Processor;
class AAC_Audio_Decoder;
class AAC_Data_Decoder;
class MOT_Slideshow_Processor;
class PCM_Player;

class BasicAudioChannel: public BasicThreadedChannel
{
public:
    bool is_selected = false;
private:
    const DAB_Parameters params;
    const Subchannel subchannel;
    MSC_Decoder* msc_decoder;
    AAC_Frame_Processor* aac_frame_processor;
    AAC_Audio_Decoder* aac_audio_decoder;
    AAC_Data_Decoder* aac_data_decoder;
    MOT_Slideshow_Processor* slideshow_processor;
    PCM_Player* pcm_player;

    const viterbi_bit_t* msc_bits_buf = NULL;
    int nb_msc_bits = 0;

    // Programme associated data
    std::string dynamic_label;
public:
    BasicAudioChannel(const DAB_Parameters _params, const Subchannel _subchannel);
    ~BasicAudioChannel();
    void SetBuffer(const viterbi_bit_t* _buf, const int _N);
    const std::string& GetDynamicLabel(void) const { return dynamic_label; }
protected:
    virtual void BeforeRun();
    virtual void Run();
};