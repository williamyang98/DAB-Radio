#pragma once

#include <memory>
#include "basic_radio/basic_radio.h"
#include "../audio/audio_pipeline.h"

static void attach_audio_pipeline_to_radio(std::shared_ptr<AudioPipeline> audio_pipeline, BasicRadio& basic_radio) {
    if (audio_pipeline == nullptr) return;
    basic_radio.On_DAB_Plus_Channel().Attach(
        [audio_pipeline](subchannel_id_t subchannel_id, Basic_DAB_Plus_Channel& channel) {
            auto& controls = channel.GetControls();
            auto audio_source = std::make_shared<AudioPipelineSource>();
            audio_pipeline->add_source(audio_source);
            channel.OnAudioData().Attach(
                [&controls, audio_source, audio_pipeline]
                (BasicAudioParams params, tcb::span<const uint8_t> buf) {
                    if (!controls.GetIsPlayAudio()) return;
                    auto frame_ptr = reinterpret_cast<const Frame<int16_t>*>(buf.data());
                    const size_t total_frames = buf.size() / sizeof(Frame<int16_t>);
                    auto frame_buf = tcb::span(frame_ptr, total_frames);
                    const bool is_blocking = audio_pipeline->get_sink() != nullptr;
                    audio_source->write(frame_buf, float(params.frequency), is_blocking);
                }
            );
        }
    );
}

