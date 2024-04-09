#include "./basic_dab_plus_channel.h"
#include <stdint.h>
#include <memory>
#include <string>
#include <string_view>
#include <fmt/format.h>
#include "dab/audio/aac_audio_decoder.h"
#include "dab/audio/aac_data_decoder.h"
#include "dab/audio/aac_frame_processor.h"
#include "dab/constants/dab_parameters.h"
#include "dab/database/dab_database_entities.h"
#include "dab/mot/MOT_entities.h"
#include "dab/msc/msc_decoder.h"
#include "utility/span.h"
#include "viterbi_config.h"
#include "./basic_audio_channel.h"
#include "./basic_audio_params.h"
#include "./basic_radio_logging.h"
#include "./basic_slideshow.h"
#define LOG_MESSAGE(...) BASIC_RADIO_LOG_MESSAGE(fmt::format(__VA_ARGS__))
#define LOG_ERROR(...) BASIC_RADIO_LOG_ERROR(fmt::format(__VA_ARGS__))

Basic_DAB_Plus_Channel::Basic_DAB_Plus_Channel(const DAB_Parameters& params, const Subchannel subchannel, const AudioServiceType audio_service_type)
: Basic_Audio_Channel(params, subchannel, audio_service_type)
{
    m_aac_frame_processor = std::make_unique<AAC_Frame_Processor>();
    m_aac_audio_decoder = nullptr;
    m_aac_data_decoder = std::make_unique<AAC_Data_Decoder>();
    SetupCallbacks();
}

Basic_DAB_Plus_Channel::~Basic_DAB_Plus_Channel() = default;

void Basic_DAB_Plus_Channel::Process(tcb::span<const viterbi_bit_t> msc_bits_buf) {
    BASIC_RADIO_SET_THREAD_NAME(fmt::format("MSC-dab-plus-subchannel-{}", m_subchannel.id));

    const int nb_msc_bits = (int)msc_bits_buf.size();
    if (nb_msc_bits != m_params.nb_msc_bits) {
        LOG_ERROR("Got incorrect number of MSC bits {}/{}", nb_msc_bits, m_params.nb_msc_bits);
        return;
    }

    if (!m_controls.GetAnyEnabled()) {
        return;
    }

    for (int i = 0; i < m_params.nb_cifs; i++) {
        const auto cif_buf = msc_bits_buf.subspan(
            i*m_params.nb_cif_bits, 
              m_params.nb_cif_bits);
        const auto decoded_bytes = m_msc_decoder->DecodeCIF(cif_buf);
        // The MSC decoder can have 0 bytes if the deinterleaver is still collecting frames
        if (decoded_bytes.empty()) {
            continue;
        }
        m_aac_frame_processor->Process(decoded_bytes);
    }
}

void Basic_DAB_Plus_Channel::SetupCallbacks(void) {
    // Decode audio
    m_aac_frame_processor->OnSuperFrameHeader().Attach([this](SuperFrameHeader header) {
        m_super_frame_header = header;

        AAC_Audio_Decoder::Params audio_params;
        audio_params.sampling_frequency = header.sampling_rate;
        audio_params.is_PS = header.PS_flag;
        audio_params.is_SBR = header.SBR_flag;
        audio_params.is_stereo = header.is_stereo;

        const bool replace_decoder = 
            (m_aac_audio_decoder == nullptr) ||
            (m_aac_audio_decoder->GetParams() != audio_params);
 
        if (replace_decoder) {
            m_aac_audio_decoder = std::make_unique<AAC_Audio_Decoder>(audio_params);
        }
    });

    // Decode audio
    m_aac_frame_processor->OnAccessUnit().Attach([this](const int au_index, const int nb_aus, tcb::span<uint8_t> buf) {
        if (!m_controls.GetIsDecodeAudio()) {
            return;
        }

        if (m_aac_audio_decoder == nullptr) {
            return;
        }
 
        if (!buf.empty()) {
            auto header = m_aac_audio_decoder->GetMPEG4Header(uint16_t(buf.size()));
            m_obs_aac_data.Notify(m_super_frame_header, header, buf);
        }

        const auto res = m_aac_audio_decoder->DecodeFrame(buf);
        // reset error flag on new superframe
        if (au_index == 0) {
            m_is_codec_error = res.is_error;
        }
        if (res.is_error) {
            LOG_ERROR("[aac-audio-decoder] error={} au_index={}/{}", 
                res.error_code, au_index, nb_aus);
            m_is_codec_error = true;
            return;
        }

        const auto audio_params = m_aac_audio_decoder->GetParams();
        BasicAudioParams params;
        params.frequency = audio_params.sampling_frequency;
        params.is_stereo = true;
        params.bytes_per_sample = 2;
        m_obs_audio_data.Notify(params, res.audio_buf);
    });

    // Decode data
    m_aac_frame_processor->OnAccessUnit().Attach([this](const int au_index, const int nb_aus, tcb::span<uint8_t> buf) {
        if (!m_controls.GetIsDecodeData()) {
            return;
        }
        m_aac_data_decoder->ProcessAccessUnit(buf);
    });

    auto& pad_processor = m_aac_data_decoder->Get_PAD_Processor();
    pad_processor.OnLabelUpdate().Attach([this](std::string_view label_str, const uint8_t charset) {
        m_dynamic_label = std::string(label_str);
        m_obs_dynamic_label.Notify(m_dynamic_label);
        LOG_MESSAGE("dynamic_label[{}]={} | charset={}", label_str.size(), label_str, charset);
    });

    pad_processor.OnMOTUpdate().Attach([this](MOT_Entity entity) {
        auto slideshow = m_slideshow_manager->Process_MOT_Entity(entity);
        if (slideshow == nullptr) {
            m_obs_MOT_entity.Notify(entity);
        }
    });

    // Listen for errors
    m_aac_frame_processor->OnFirecodeError().Attach([this](int frame_index, uint16_t crc_got, uint16_t crc_calc) {
        m_is_firecode_error = true;
    });

    m_aac_frame_processor->OnRSError().Attach([this](int au_index, int total_aus) {
        m_is_rs_error = true;
    });

    m_aac_frame_processor->OnSuperFrameHeader().Attach([this](SuperFrameHeader header) {
        m_is_firecode_error = false;
        m_is_rs_error = false;
    });

    m_aac_frame_processor->OnAccessUnitCRCError().Attach([this](int au_index, int nb_aus, uint16_t crc_got, uint16_t crc_calc) {
        m_is_au_error = true;
    });

    m_aac_frame_processor->OnAccessUnit().Attach([this](int au_index, int nb_aus, tcb::span<uint8_t> data) {
        if (au_index == 0) {
            m_is_au_error = false;
        }
    });
}
