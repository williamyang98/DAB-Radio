#include "render_portaudio_controls.h"

#include <imgui.h>

void RenderPortAudioControls(PaDeviceList& device_list, PortAudio_Output& audio_output) {
    auto& devices = device_list.GetDevices();

    const auto selected_index = audio_output.GetSelectedIndex();
    const char* selected_name = "Unselected";
    for (auto& device: devices) {
        if (device.index == selected_index) {
            selected_name = device.label.c_str();
            break;
        }
    }

    ImGui::Text("Output Devices (%d)", (int)devices.size());
    ImGui::PushItemWidth(-1.0f);
    if (ImGui::BeginCombo("###Output Devices", selected_name, ImGuiComboFlags_None)) {
        for (auto& device: devices) {
            const bool is_selected = (device.index == selected_index);
            ImGui::PushID(device.index);
            if (ImGui::Selectable(device.label.c_str(), is_selected)) {
                if (!is_selected) {
                    audio_output.Open(device.index);
                }
            }
            if (is_selected) {
                ImGui::SetItemDefaultFocus();
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    ImGui::PopItemWidth();

    auto& mixer = audio_output.GetMixer();
    auto& volume_gain = mixer.GetOutputGain();

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