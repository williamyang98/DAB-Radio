#pragma once

#include <stdint.h>
#include <memory>
#include "dab/audio/aac_frame_processor.h"
#include "dab/constants/dab_parameters.h"
#include "dab/database/dab_database_entities.h"
#include "utility/observable.h"
#include "utility/span.h"
#include "viterbi_config.h"
#include "./basic_audio_channel.h"

class AAC_Audio_Decoder;
class AAC_Data_Decoder;

// Audio channel player for DAB+
class Basic_DAB_Plus_Channel: public Basic_Audio_Channel
{
private:
    std::unique_ptr<AAC_Frame_Processor> m_aac_frame_processor;
    std::unique_ptr<AAC_Audio_Decoder> m_aac_audio_decoder;
    std::unique_ptr<AAC_Data_Decoder> m_aac_data_decoder;
    SuperFrameHeader m_super_frame_header;
    bool m_is_firecode_error = false;
    bool m_is_rs_error = false;
    bool m_is_au_error = false;
    bool m_is_codec_error = false;
    // superframe, header, audio_frame_data
    Observable<SuperFrameHeader, tcb::span<const uint8_t>, tcb::span<const uint8_t>> m_obs_aac_data;
public:
    explicit Basic_DAB_Plus_Channel(const DAB_Parameters& params, const Subchannel subchannel, const AudioServiceType audio_service_type);
    ~Basic_DAB_Plus_Channel() override;
    void Process(tcb::span<const viterbi_bit_t> msc_bits_buf) override;
    const auto& GetSuperFrameHeader() const { return m_super_frame_header; }
    bool IsFirecodeError() const { return m_is_firecode_error; }
    bool IsRSError() const { return m_is_rs_error; }
    bool IsAUError() const { return m_is_au_error; }
    bool IsCodecError() const { return m_is_codec_error; }
    auto& OnAACData() { return m_obs_aac_data; }
private:
    void SetupCallbacks(void);
};
