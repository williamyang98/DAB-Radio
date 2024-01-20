#include "./render_portaudio_controls.h"
#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>
#include "audio/portaudio_sink.h"

void RenderPortAudioControls(PaDeviceList& device_list, AudioPipeline& pipeline) {
    auto& devices = device_list.GetDevices();
    const auto* sink = pipeline.get_sink();
    const char* selected_name = "Unselected";
    if (sink != nullptr) {
        selected_name = sink->get_name().data(); 
    }

    ImGui::Text("Output Devices (%d)", int(devices.size()));
    ImGui::PushItemWidth(-1.0f);
    if (devices.size() == 0) ImGui::BeginDisabled();
    if (ImGui::BeginCombo("###Output Devices", selected_name, ImGuiComboFlags_None)) {
        for (auto& device: devices) {
            ImGui::PushID(device.index);
            if (ImGui::Selectable(device.label.c_str(), false)) {
                auto sink_res = PortAudioSink::create_from_index(device.index);
                pipeline.set_sink(std::move(sink_res.sink));
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    if (devices.size() == 0) ImGui::EndDisabled();
    ImGui::PopItemWidth();

    auto& volume_gain = pipeline.get_global_gain();

    static bool is_overgain = false;
    static float last_unmuted_volume = 0.0f;

    bool is_muted = (volume_gain == 0.0f);
    const float max_gain = is_overgain ? 6.0f : 2.0f;
    if (!is_overgain) {
        volume_gain = (volume_gain > max_gain) ? max_gain : volume_gain;
    }

    ImGui::PushItemWidth(-1.0f);
    ImGui::Text("Volume");

    const float volume_scale = 100.0f;
    float curr_volume = volume_gain * volume_scale;
    if (ImGui::SliderFloat("###Volume", &curr_volume, 0.0f, max_gain*volume_scale, "%.0f", ImGuiSliderFlags_AlwaysClamp)) {
        volume_gain = (curr_volume / volume_scale);
        if (volume_gain > 0.0f) {
            last_unmuted_volume = volume_gain;
        } else {
            last_unmuted_volume = 1.0f;
        }
    }
    ImGui::PopItemWidth();

    if (is_muted) {
        if (ImGui::Button("Unmute")) {
            volume_gain = last_unmuted_volume;
        }
    } else {
        if (ImGui::Button("Mute")) {
            last_unmuted_volume = volume_gain;
            volume_gain = 0.0f;
        }
    }

    ImGui::SameLine();

    if (ImGui::Button(is_overgain ? "Normal gain" : "Boost gain")) {
        is_overgain = !is_overgain;
    }
}