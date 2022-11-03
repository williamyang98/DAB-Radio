#include "basic_audio_channel.h"

#include "dab/msc/msc_decoder.h"
#include "dab/audio/aac_frame_processor.h"
#include "dab/audio/aac_audio_decoder.h"
#include "dab/audio/aac_data_decoder.h"
#include "dab/mot/mot_slideshow_processor.h"

#include "audio/win32_pcm_player.h"

#include "easylogging++.h"
#include "fmt/core.h"

#define LOG_MESSAGE(...) CLOG(INFO, "basic-radio") << fmt::format(##__VA_ARGS__)
#define LOG_ERROR(...) CLOG(ERROR, "basic-radio") << fmt::format(##__VA_ARGS__)

BasicAudioChannel::BasicAudioChannel(const DAB_Parameters _params, const Subchannel _subchannel) 
: params(_params), subchannel(_subchannel) {
    msc_decoder = new MSC_Decoder(subchannel);
    aac_frame_processor = new AAC_Frame_Processor();
    aac_audio_decoder = NULL;
    aac_data_decoder = new AAC_Data_Decoder();
    slideshow_processor = new MOT_Slideshow_Processor();

    pcm_player = new Win32_PCM_Player();

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

    aac_frame_processor->OnAccessUnit().Attach([this](const int au_index, const int nb_aus, uint8_t* buf, const int N) {
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
        auto pcm_params = pcm_player->GetParameters();
        pcm_params.sample_rate = audio_params.sampling_frequency;
        pcm_params.total_channels = 2;
        pcm_params.bytes_per_sample = 2;
        pcm_player->SetParameters(pcm_params);
        pcm_player->ConsumeBuffer(res.audio_buf, res.nb_audio_buf_bytes);
    });

    aac_frame_processor->OnAccessUnit().Attach([this](const int au_index, const int nb_aus, uint8_t* buf, const int N) {
        aac_data_decoder->ProcessAccessUnit(buf, N);
    });

    auto& pad_processor = aac_data_decoder->Get_PAD_Processor();
    pad_processor.OnLabelUpdate().Attach([this](const uint8_t* label, const int N) {
        const auto* label_str = reinterpret_cast<const char*>(label);
        dynamic_label = std::string(label_str, N);
        LOG_MESSAGE("[pad-processor] dynamic_label[{}] = {}", N, dynamic_label);
    });

    pad_processor.OnMOTUpdate().Attach([this](MOT_Entity entity) {
        // DOC: ETSI TS 101 756
        // Table 17: Content type and content subtypes 

        // DOC: ETSI TS 101 499
        // Clause 6.2.3 MOT ContentTypes and ContentSubTypes 
        // For specific types used for slideshows

        const auto type = entity.header.content_type;
        const auto sub_type = entity.header.content_sub_type;
        // Content type: Image
        if (type != 0b000010) {
            return;
        }
        // Content subtype: JPEG and PNG
        if ((sub_type != 0b01) && (sub_type != 0b11)) {
            return;
        }

        const bool is_jpg = (sub_type == 0b01);

        // User application header extension parameters
        MOT_Slideshow slideshow_data;
        for (auto& p: entity.header.user_app_params) {
            slideshow_processor->ProcessHeaderExtension(
                &slideshow_data, 
                p.type, p.data, p.nb_data_bytes);
        }

        static char filename[256] = {0};
        if (entity.header.content_name.exists) {
            snprintf(filename, sizeof(filename), "images/subchannel_%d_%.*s", 
                subchannel.id,
                entity.header.content_name.nb_bytes,
                entity.header.content_name.name);
        } else {
            snprintf(filename, sizeof(filename), "images/subchannel_%d_tid_%d.%s", 
                subchannel.id, entity.transport_id, is_jpg ? "jpg" : "png");
        } 

        // FILE* fp = fopen(filename, "wb+");
        // if (fp == NULL) {
        //     LOG_ERROR("Failed to write slideshow {}", filename);
        //     return;
        // }
        // fwrite(entity.body_buf, sizeof(uint8_t), entity.nb_body_bytes, fp);
        // fclose(fp);

        LOG_MESSAGE("Wrote image to {}", filename);
    });
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
    delete slideshow_processor;
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