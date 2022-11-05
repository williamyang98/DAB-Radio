#include "basic_audio_channel.h"

#include "dab/msc/msc_decoder.h"
#include "dab/audio/aac_frame_processor.h"
#include "dab/audio/aac_audio_decoder.h"
#include "dab/audio/aac_data_decoder.h"
#include "dab/mot/mot_slideshow_processor.h"

#include "easylogging++.h"
#include "fmt/core.h"

#define LOG_MESSAGE(...) CLOG(INFO, "basic-radio") << fmt::format(##__VA_ARGS__)
#define LOG_ERROR(...) CLOG(ERROR, "basic-radio") << fmt::format(##__VA_ARGS__)

BasicAudioChannel::BasicAudioChannel(
    const DAB_Parameters _params, const Subchannel _subchannel, 
    Basic_Radio_Dependencies* dependencies) 
: params(_params), subchannel(_subchannel) {
    msc_decoder = new MSC_Decoder(subchannel);
    aac_frame_processor = new AAC_Frame_Processor();
    aac_audio_decoder = NULL;
    aac_data_decoder = new AAC_Data_Decoder();

    pcm_player = dependencies->Create_PCM_Player();
    slideshow_manager = new Basic_Slideshow_Manager();

    SetupCallbacks();
}

BasicAudioChannel::~BasicAudioChannel() {
    Stop();
    Join();
    delete msc_decoder;
    delete aac_frame_processor;
    if (aac_audio_decoder != NULL) {
        delete aac_audio_decoder;
    }
    delete aac_data_decoder;
    delete slideshow_manager;
    delete pcm_player;
}

void BasicAudioChannel::SetBuffer(const viterbi_bit_t* _buf, const int _N) {
    msc_bits_buf = _buf;
    nb_msc_bits = _N;
}

void BasicAudioChannel::BeforeRun() {
    el::Helpers::setThreadName(fmt::format("MSC-subchannel-{}", subchannel.id));
}

void BasicAudioChannel::Run() {
    if (nb_msc_bits != params.nb_msc_bits) {
        LOG_ERROR("Got incorrect number of MSC bits {}/{}", nb_msc_bits, params.nb_msc_bits);
        return;
    }

    if (msc_bits_buf == NULL) {
        LOG_ERROR("Got NULL for msc bits buffer");
        return;
    }

    if (!controls.GetAnyEnabled()) {
        return;
    }

    for (int i = 0; i < params.nb_cifs; i++) {
        const auto* cif_buf = &msc_bits_buf[params.nb_cif_bits*i];
        const int nb_decoded_bytes = msc_decoder->DecodeCIF(cif_buf, params.nb_cif_bits);
        // The MSC decoder can have 0 bytes if the deinterleaver is still collecting frames
        if (nb_decoded_bytes == 0) {
            continue;
        }
        const auto* decoded_buf = msc_decoder->GetDecodedBytes();
        aac_frame_processor->Process(decoded_buf, nb_decoded_bytes);
    }
}

void BasicAudioChannel::SetupCallbacks(void) {
    // Decode audio
    aac_frame_processor->OnSuperFrameHeader().Attach([this](SuperFrameHeader header) {
        AAC_Audio_Decoder::Params audio_params;
        audio_params.sampling_frequency = header.sampling_rate;
        audio_params.is_PS = header.PS_flag;
        audio_params.is_SBR = header.SBR_flag;
        audio_params.is_stereo = header.is_stereo;

        if (aac_audio_decoder == NULL) {
            aac_audio_decoder = new AAC_Audio_Decoder(audio_params);
            return;
        }

        const auto old_audio_params = aac_audio_decoder->GetParams();
        if (old_audio_params != audio_params) {
            delete aac_audio_decoder;
            aac_audio_decoder = new AAC_Audio_Decoder(audio_params);
        }
    });

    // Decode audio
    aac_frame_processor->OnAccessUnit().Attach([this](const int au_index, const int nb_aus, uint8_t* buf, const int N) {
        if (!controls.GetIsDecodeAudio()) {
            return;
        }

        if (aac_audio_decoder == NULL) {
            return;
        }
        const auto res = aac_audio_decoder->DecodeFrame(buf, N);
        if (res.is_error) {
            LOG_ERROR("[aac-audio-decoder] error={} au_index={}/{}", 
                res.error_code, au_index, nb_aus);
            return;
        }

        const auto audio_params = aac_audio_decoder->GetParams();
        BasicAudioParams params;
        params.frequency = audio_params.sampling_frequency;
        params.is_stereo = true;
        params.bytes_per_sample = 2;
        obs_audio_data.Notify(params, res.audio_buf, res.nb_audio_buf_bytes);
    });

    // Play audio through device
    obs_audio_data.Attach([this](BasicAudioParams params, const uint8_t* data, const int N) {
        if (!controls.GetIsPlayAudio()) {
            return;
        }

        auto pcm_params = pcm_player->GetParameters();
        pcm_params.sample_rate = params.frequency;
        pcm_params.total_channels = 2;
        pcm_params.bytes_per_sample = 2;
        pcm_player->SetParameters(pcm_params);
        pcm_player->ConsumeBuffer(data, N);
    });

    // Decode data
    aac_frame_processor->OnAccessUnit().Attach([this](const int au_index, const int nb_aus, uint8_t* buf, const int N) {
        if (!controls.GetIsDecodeData()) {
            return;
        }

        aac_data_decoder->ProcessAccessUnit(buf, N);
    });

    auto& pad_processor = aac_data_decoder->Get_PAD_Processor();
    pad_processor.OnLabelUpdate().Attach([this](const uint8_t* label, const int N, const uint8_t charset) {
        const auto* label_str = reinterpret_cast<const char*>(label);
        dynamic_label = std::string(label_str, N);
        obs_dynamic_label.Notify(dynamic_label);
        LOG_MESSAGE("dynamic_label[{}]={} | charset={}", N, dynamic_label, charset);
    });

    pad_processor.OnMOTUpdate().Attach([this](MOT_Entity entity) {
        auto* res = slideshow_manager->Process_MOT_Entity(&entity);
        if (res != NULL) {
            obs_slideshow.Notify(res);
        } else {
            obs_MOT_entity.Notify(&entity);
        }
    });
}

// controls
constexpr uint8_t CONTROL_FLAG_DECODE_AUDIO = 0b10000000;
constexpr uint8_t CONTROL_FLAG_DECODE_DATA  = 0b01000000;
constexpr uint8_t CONTROL_FLAG_PLAY_AUDIO   = 0b00100000;
constexpr uint8_t CONTROL_FLAG_ALL_SELECTED = 0b11100000;

bool BasicAudioChannelControls::GetAnyEnabled(void) const {
    return (flags != 0);
}

bool BasicAudioChannelControls::GetAllEnabled(void) const {
    return (flags == CONTROL_FLAG_ALL_SELECTED);
}

void BasicAudioChannelControls::RunAll(void) {
    flags = CONTROL_FLAG_ALL_SELECTED;
}

void BasicAudioChannelControls::StopAll(void) {
    flags = 0;
}

// Decode AAC audio elements
bool BasicAudioChannelControls::GetIsDecodeAudio(void) const {
    return (flags & CONTROL_FLAG_DECODE_AUDIO) != 0;
}

void BasicAudioChannelControls::SetIsDecodeAudio(bool v) {
    SetFlag(CONTROL_FLAG_DECODE_AUDIO, v);
    if (!v) {
        SetFlag(CONTROL_FLAG_PLAY_AUDIO, false);
    }
}

// Decode AAC data_stream_element
bool BasicAudioChannelControls::GetIsDecodeData(void) const {
    return (flags & CONTROL_FLAG_DECODE_DATA) != 0;
}

void BasicAudioChannelControls::SetIsDecodeData(bool v) {
    SetFlag(CONTROL_FLAG_DECODE_DATA, v);
}

// Play audio data through sound device
bool BasicAudioChannelControls::GetIsPlayAudio(void) const {
    return (flags & CONTROL_FLAG_PLAY_AUDIO) != 0;
}

void BasicAudioChannelControls::SetIsPlayAudio(bool v) { 
    SetFlag(CONTROL_FLAG_PLAY_AUDIO, v);
    if (v) {
        SetFlag(CONTROL_FLAG_DECODE_AUDIO, true);
    }
}

void BasicAudioChannelControls::SetFlag(const uint8_t flag, const bool state) {
    if (state) {
        flags |= flag;
    } else {
        flags &= ~flag;
    }
}