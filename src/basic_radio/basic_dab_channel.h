#pragma once

#include <stdint.h>
#include <memory>
#include <optional>
#include <vector>
#include "dab/constants/dab_parameters.h"
#include "dab/database/dab_database_entities.h"
#include "utility/observable.h"
#include "utility/span.h"
#include "viterbi_config.h"
#include "./basic_audio_channel.h"

class PAD_Processor;
struct plm_buffer_t;
struct plm_audio_t;

// Audio channel player for DAB+
class Basic_DAB_Channel: public Basic_Audio_Channel
{
public:
    struct AudioParams {
        bool is_stereo = false;
        int bitrate_kbps = 0;
        int sample_rate = 0;
    };
private:
    plm_buffer_t* m_plm_buffer;
    plm_audio_t* m_plm_audio;
    std::vector<int16_t> m_audio_data;
    std::unique_ptr<PAD_Processor> m_pad_processor;
    bool m_is_error = false;
    std::optional<AudioParams> m_audio_params = std::nullopt;
    Observable<tcb::span<const uint8_t>> m_obs_mp2_data;
public:
    explicit Basic_DAB_Channel(const DAB_Parameters& params, const Subchannel subchannel, const AudioServiceType audio_service_type);
    ~Basic_DAB_Channel() override;
    void Process(tcb::span<const viterbi_bit_t> msc_bits_buf) override;
    auto& OnMP2Data() { return m_obs_mp2_data; }
    bool GetIsError() const { return m_is_error; }
    const auto& GetAudioParams() const { return m_audio_params; }
private:
    void SetupCallbacks(void);
};
