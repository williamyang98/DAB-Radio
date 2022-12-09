#include "basic_dab_plus_channel.h"

#include "modules/dab/msc/msc_decoder.h"
#include "modules/dab/audio/aac_audio_decoder.h"
#include "modules/dab/audio/aac_data_decoder.h"
#include "modules/dab/mot/MOT_slideshow_processor.h"
#include "basic_slideshow.h"

#include "easylogging++.h"
#include "fmt/core.h"

#define LOG_MESSAGE(...) CLOG(INFO, "basic-radio") << fmt::format(__VA_ARGS__)
#define LOG_ERROR(...) CLOG(ERROR, "basic-radio") << fmt::format(__VA_ARGS__)

Basic_DAB_Plus_Channel::Basic_DAB_Plus_Channel(const DAB_Parameters _params, const Subchannel _subchannel) 
: params(_params), subchannel(_subchannel) {
    msc_decoder = std::make_unique<MSC_Decoder>(subchannel);
    aac_frame_processor = std::make_unique<AAC_Frame_Processor>();
    aac_audio_decoder = NULL;
    aac_data_decoder = std::make_unique<AAC_Data_Decoder>();
    slideshow_manager = std::make_unique<Basic_Slideshow_Manager>();
    SetupCallbacks();
}

Basic_DAB_Plus_Channel::~Basic_DAB_Plus_Channel() = default;

void Basic_DAB_Plus_Channel::Process(tcb::span<const viterbi_bit_t> msc_bits_buf) {
    el::Helpers::setThreadName(fmt::format("MSC-subchannel-{}", subchannel.id));

    const int nb_msc_bits = (int)msc_bits_buf.size();
    if (nb_msc_bits != params.nb_msc_bits) {
        LOG_ERROR("Got incorrect number of MSC bits {}/{}", nb_msc_bits, params.nb_msc_bits);
        return;
    }

    if (!controls.GetAnyEnabled()) {
        return;
    }

    for (int i = 0; i < params.nb_cifs; i++) {
        const auto cif_buf = msc_bits_buf.subspan(
            i*params.nb_cif_bits, 
              params.nb_cif_bits);
        const auto decoded_bytes = msc_decoder->DecodeCIF(cif_buf);
        // The MSC decoder can have 0 bytes if the deinterleaver is still collecting frames
        if (decoded_bytes.empty()) {
            continue;
        }
        aac_frame_processor->Process(decoded_bytes);
    }
}

void Basic_DAB_Plus_Channel::SetupCallbacks(void) {
    // Decode audio
    aac_frame_processor->OnSuperFrameHeader().Attach([this](SuperFrameHeader header) {
        super_frame_header = header;

        AAC_Audio_Decoder::Params audio_params;
        audio_params.sampling_frequency = header.sampling_rate;
        audio_params.is_PS = header.PS_flag;
        audio_params.is_SBR = header.SBR_flag;
        audio_params.is_stereo = header.is_stereo;

        const bool replace_decoder = 
            (aac_audio_decoder == NULL) ||
            (aac_audio_decoder->GetParams() != audio_params);
        
        if (replace_decoder) {
            aac_audio_decoder = std::make_unique<AAC_Audio_Decoder>(audio_params);
        }
    });

    // Decode audio
    aac_frame_processor->OnAccessUnit().Attach([this](const int au_index, const int nb_aus, tcb::span<uint8_t> buf) {
        if (!controls.GetIsDecodeAudio()) {
            return;
        }

        if (aac_audio_decoder == NULL) {
            return;
        }
        const auto res = aac_audio_decoder->DecodeFrame(buf);
        // reset error flag on new superframe
        if (au_index == 0) {
            is_codec_error = res.is_error;
        }
        if (res.is_error) {
            LOG_ERROR("[aac-audio-decoder] error={} au_index={}/{}", 
                res.error_code, au_index, nb_aus);
            is_codec_error = true;
            return;
        }

        const auto audio_params = aac_audio_decoder->GetParams();
        BasicAudioParams params;
        params.frequency = audio_params.sampling_frequency;
        params.is_stereo = true;
        params.bytes_per_sample = 2;
        obs_audio_data.Notify(params, res.audio_buf);
    });

    // Decode data
    aac_frame_processor->OnAccessUnit().Attach([this](const int au_index, const int nb_aus, tcb::span<uint8_t> buf) {
        if (!controls.GetIsDecodeData()) {
            return;
        }
        aac_data_decoder->ProcessAccessUnit(buf);
    });

    auto& pad_processor = aac_data_decoder->Get_PAD_Processor();
    pad_processor.OnLabelUpdate().Attach([this](std::string_view label_str, const uint8_t charset) {
        dynamic_label = std::string(label_str);
        obs_dynamic_label.Notify(dynamic_label);
        LOG_MESSAGE("dynamic_label[{}]={} | charset={}", label_str.size(), label_str, charset);
    });

    pad_processor.OnMOTUpdate().Attach([this](MOT_Entity entity) {
        auto* res = slideshow_manager->Process_MOT_Entity(entity);
        if (res != NULL) {
            obs_slideshow.Notify(*res);
        } else {
            obs_MOT_entity.Notify(entity);
        }
    });

    // Listen for errors
    aac_frame_processor->OnFirecodeError().Attach([this](int frame_index, uint16_t crc_got, uint16_t crc_calc) {
        is_firecode_error = true;
    });

    aac_frame_processor->OnRSError().Attach([this](int au_index, int total_aus) {
        is_rs_error = true;
    });

    aac_frame_processor->OnSuperFrameHeader().Attach([this](SuperFrameHeader header) {
        is_firecode_error = false;
        is_rs_error = false;
    });

    aac_frame_processor->OnAccessUnitCRCError().Attach([this](int au_index, int nb_aus, uint16_t crc_got, uint16_t crc_calc) {
        is_au_error = true;
    });

    aac_frame_processor->OnAccessUnit().Attach([this](int au_index, int nb_aus, tcb::span<uint8_t> data) {
        if (au_index == 0) {
            is_au_error = false;
        }
    });
}

// controls
constexpr uint8_t CONTROL_FLAG_DECODE_AUDIO = 0b10000000;
constexpr uint8_t CONTROL_FLAG_DECODE_DATA  = 0b01000000;
constexpr uint8_t CONTROL_FLAG_PLAY_AUDIO   = 0b00100000;
constexpr uint8_t CONTROL_FLAG_ALL_SELECTED = 0b11100000;

bool Basic_DAB_Plus_Controls::GetAnyEnabled(void) const {
    return (flags != 0);
}

bool Basic_DAB_Plus_Controls::GetAllEnabled(void) const {
    return (flags == CONTROL_FLAG_ALL_SELECTED);
}

void Basic_DAB_Plus_Controls::RunAll(void) {
    flags = CONTROL_FLAG_ALL_SELECTED;
}

void Basic_DAB_Plus_Controls::StopAll(void) {
    flags = 0;
}

// Decode AAC audio elements
bool Basic_DAB_Plus_Controls::GetIsDecodeAudio(void) const {
    return (flags & CONTROL_FLAG_DECODE_AUDIO) != 0;
}

void Basic_DAB_Plus_Controls::SetIsDecodeAudio(bool v) {
    SetFlag(CONTROL_FLAG_DECODE_AUDIO, v);
    if (!v) {
        SetFlag(CONTROL_FLAG_PLAY_AUDIO, false);
    }
}

// Decode AAC data_stream_element
bool Basic_DAB_Plus_Controls::GetIsDecodeData(void) const {
    return (flags & CONTROL_FLAG_DECODE_DATA) != 0;
}

void Basic_DAB_Plus_Controls::SetIsDecodeData(bool v) {
    SetFlag(CONTROL_FLAG_DECODE_DATA, v);
}

// Play audio data through sound device
bool Basic_DAB_Plus_Controls::GetIsPlayAudio(void) const {
    return (flags & CONTROL_FLAG_PLAY_AUDIO) != 0;
}

void Basic_DAB_Plus_Controls::SetIsPlayAudio(bool v) { 
    SetFlag(CONTROL_FLAG_PLAY_AUDIO, v);
    if (v) {
        SetFlag(CONTROL_FLAG_DECODE_AUDIO, true);
    }
}

void Basic_DAB_Plus_Controls::SetFlag(const uint8_t flag, const bool state) {
    if (state) {
        flags |= flag;
    } else {
        flags &= ~flag;
    }
}