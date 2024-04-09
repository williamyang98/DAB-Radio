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
    m_plm_buffer = plm_buffer_create_with_capacity(32);
    m_plm_audio = plm_audio_create_with_buffer(m_plm_buffer);
    m_pad_processor = std::make_unique<PAD_Processor>();
    SetupCallbacks();
}

Basic_DAB_Channel::~Basic_DAB_Channel() {
    plm_audio_destroy(m_plm_audio);
    plm_buffer_destroy(m_plm_buffer);
    m_plm_audio = nullptr;
    m_plm_buffer = nullptr;
};

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
 
        plm_buffer_rewind(m_plm_buffer); // we can assume full frames are decoded each time
        plm_buffer_write(m_plm_buffer, decoded_bytes.data(), decoded_bytes.size());
        const int total_data_bytes = plm_audio_decode_header(m_plm_audio);
        if (total_data_bytes == 0) {
            m_is_error = true;
            continue;
        }

        plm_samples_t* samples = plm_audio_decode(m_plm_audio, total_data_bytes);
        if (samples == nullptr) {
            m_is_error = true;
            continue;
        }
        m_is_error = false;

        const int bitrate_kbps = plm_audio_get_bitrate(m_plm_audio);
        const int total_channels = plm_audio_get_channels(m_plm_audio);
        const int sample_rate = plm_audio_get_samplerate(m_plm_audio);
        const bool is_stereo = (total_channels == 2);
        m_audio_params = std::optional<AudioParams>({ is_stereo, bitrate_kbps, sample_rate });
 
        // TODO: In order to decode the PAD we need to determine where the mp2 decoder stops reading
        //       Is there a more sensible way to do this?
        if (m_controls.GetIsDecodeData()) {
            const int bitrate_per_channel = bitrate_kbps/total_channels;
            // DOC: ETSI TS 103 466
            // Figure 6: DAB audio frame structure
            const int total_crc_bytes = (bitrate_per_channel >= 56) ? 4 : 2;
            const int total_fpad_bytes = 2;
            // Determine number of xpad bytes
            const int total_audio_frame_bytes = int(plm_buffer_get_read_head_bytes(m_plm_buffer));
            const int total_pad_bytes = int(decoded_bytes.size()) - total_audio_frame_bytes;
            const int total_xpad_bytes = total_pad_bytes-total_crc_bytes-total_fpad_bytes;
            if (total_xpad_bytes >= 0) {
                auto pad = decoded_bytes.subspan(size_t(total_audio_frame_bytes));
                auto fpad = pad.last(size_t(total_fpad_bytes));
                auto xpad = pad.first(size_t(total_xpad_bytes));
                m_pad_processor->Process(fpad, xpad);
            }
        }

        if (m_controls.GetIsPlayAudio()) {
            constexpr float gain = float(std::numeric_limits<int16_t>::max()-1);
            const size_t N = size_t(samples->count*2);
            m_audio_data.resize(N);
            for (size_t j = 0; j < N; j++) {
                float v = samples->interleaved[j];
                if (v > 1.0) v = 1.0;
                else if (v < -1.0) v = -1.0;
                int16_t v_scale = int16_t(v*gain);
                m_audio_data[j] = v_scale;
            }

            const size_t total_bytes = N*sizeof(int16_t);
            auto data = tcb::span(reinterpret_cast<const uint8_t*>(m_audio_data.data()), total_bytes);
            BasicAudioParams params;
            params.frequency = uint32_t(sample_rate);
            params.bytes_per_sample = 2;
            params.is_stereo = true;
            m_obs_audio_data.Notify(params, data);
        }
    }
}

void Basic_DAB_Channel::SetupCallbacks(void) {
    m_pad_processor->OnLabelUpdate().Attach([this](std::string_view label_str, const uint8_t charset) {
        m_dynamic_label = std::string(label_str);
        m_obs_dynamic_label.Notify(m_dynamic_label);
        LOG_MESSAGE("dynamic_label[{}]={} | charset={}", label_str.size(), label_str, charset);
    });

    m_pad_processor->OnMOTUpdate().Attach([this](MOT_Entity entity) {
        auto slideshow = m_slideshow_manager->Process_MOT_Entity(entity);
        if (slideshow == nullptr) {
            m_obs_MOT_entity.Notify(entity);
        }
    });
}

