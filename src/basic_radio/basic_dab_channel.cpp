#include "./basic_dab_channel.h"
#include <stddef.h>
#include <stdint.h>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <fmt/format.h>
#include "dab/audio/mp2_audio_decoder.h"
#include "dab/constants/dab_parameters.h"
#include "dab/database/dab_database_entities.h"
#include "dab/msc/msc_decoder.h"
#include "dab/pad/pad_processor.h"
#include "utility/span.h"
#include "viterbi_config.h"
#include "./basic_audio_channel.h"
#include "./basic_audio_params.h"
#include "./basic_radio_logging.h"
#include "./basic_slideshow.h"
#define LOG_MESSAGE(...) BASIC_RADIO_LOG_MESSAGE(fmt::format(__VA_ARGS__))
#define LOG_ERROR(...) BASIC_RADIO_LOG_ERROR(fmt::format(__VA_ARGS__))
#undef min // NOLINT
#undef max // NOLINT

Basic_DAB_Channel::Basic_DAB_Channel(const DAB_Parameters& params, const Subchannel subchannel, const AudioServiceType audio_service_type)
: Basic_Audio_Channel(params, subchannel, audio_service_type) 
{
    m_pad_processor = std::make_unique<PAD_Processor>();
    m_mp2_decoder = std::make_unique<MP2_Audio_Decoder>();
    SetupCallbacks();
}

Basic_DAB_Channel::~Basic_DAB_Channel() {}

void Basic_DAB_Channel::Process(tcb::span<const viterbi_bit_t> msc_bits_buf) {
    BASIC_RADIO_SET_THREAD_NAME(fmt::format("MSC-dab-subchannel-{}", m_subchannel.id));

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

        m_obs_mp2_data.Notify(decoded_bytes);

        if (!m_controls.GetAnyEnabled()) { 
            continue;
        }

        const auto res = m_mp2_decoder->decode_frame(decoded_bytes);
        if (!res.has_value()) {
            m_is_error = true;
            continue;
        }

        m_is_error = false;
        const auto& frame = res.value();
 
        m_audio_params = frame.frame_header;
 
        if (m_controls.GetIsDecodeData()) {
            m_pad_processor->Process(frame.fpad_data, frame.xpad_data);
        }

        if (m_controls.GetIsPlayAudio()) {
            const auto audio_data = frame.audio_data;
            if (frame.frame_header.is_stereo) {
                const size_t N = audio_data.size();
                m_audio_data.resize(N);
                for (size_t j = 0; j < N; j++) {
                    m_audio_data[j] = audio_data[j];
                }
            } else {
                // split out mono data
                const size_t N = audio_data.size();
                m_audio_data.resize(2*N);
                for (size_t j = 0; j < N; j++) {
                    const int16_t v = audio_data[j];
                    const size_t k = 2*j;
                    m_audio_data[k] = v;
                    m_audio_data[k+1] = v;
                }
            }

            const size_t total_bytes = m_audio_data.size()*sizeof(int16_t);
            const auto data = tcb::span(reinterpret_cast<const uint8_t*>(m_audio_data.data()), total_bytes);
            BasicAudioParams params;
            params.frequency = uint32_t(frame.frame_header.sample_rate);
            params.bytes_per_sample = 2;
            params.is_stereo = true;
            m_obs_audio_data.Notify(params, data);
        }
    }
}

void Basic_DAB_Channel::SetupCallbacks(void) {
    m_pad_processor->OnLabelUpdate().Attach([this](const std::string& label) {
        m_dynamic_label = label;
        m_obs_dynamic_label.Notify(m_dynamic_label);
        LOG_MESSAGE("dynamic_label={}", label);
    });

    m_pad_processor->OnMOTUpdate().Attach([this](MOT_Entity entity) {
        auto slideshow = m_slideshow_manager->Process_MOT_Entity(entity);
        if (slideshow == nullptr) {
            m_obs_MOT_entity.Notify(entity);
        }
    });
}

