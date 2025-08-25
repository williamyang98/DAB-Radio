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
#include "dab/audio/mp2_audio_decoder.h"

class PAD_Processor;
class MP2_Audio_Decoder;

// Audio channel player for DAB+
class Basic_DAB_Channel: public Basic_Audio_Channel
{
private:
    std::vector<int16_t> m_audio_data;
    std::unique_ptr<PAD_Processor> m_pad_processor;
    std::unique_ptr<MP2_Audio_Decoder> m_mp2_decoder;
    bool m_is_error = true;
    std::optional<MP2_Audio_Decoder::FrameHeader> m_audio_params = std::nullopt;
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
