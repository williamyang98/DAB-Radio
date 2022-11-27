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
}