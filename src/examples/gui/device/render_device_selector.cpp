#include "./render_device_selector.h"

#include "device/device.h"
#include "device/device_selector.h"

#include <imgui.h>
#include <fmt/core.h>

void RenderDeviceControls(Device& device, const block_frequency_table_t& block_frequencies);

void RenderDeviceSelector(DeviceSelector& app, const block_frequency_table_t& block_frequencies) {
    if (ImGui::Begin("Device Controls")) {
        if (ImGui::Button("Search")) {
            app.SearchDevices();
        }

        auto* selected_device = app.GetDevice();

        std::string preview_label;
        if (selected_device == NULL) {
            preview_label = "None";
        } else {
            const auto& descriptor = selected_device->GetDescriptor();
            preview_label = fmt::format("[{}] {}",
                descriptor.index, descriptor.product);
        }
        if (ImGui::BeginCombo("Devices", preview_label.c_str())) {
            for (auto& device: app.GetDeviceList()) {
                auto label = fmt::format("[{}] Vendor={} Product={} Serial={}",
                    device.index, device.vendor, device.product, device.serial);
                const bool is_selected = (selected_device == NULL) ? 
                    false : (selected_device->GetDescriptor().index == device.index);

                if (ImGui::Selectable(label.c_str(), is_selected)) {
                    if (is_selected) {
                        app.CloseDevice();
                    } else {
                        app.SelectDevice(device.index);
                    }
                }
                if (is_selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        {
            auto lock = std::unique_lock(app.GetDeviceMutex());
            selected_device = app.GetDevice();
            if (selected_device != NULL) {
                RenderDeviceControls(*selected_device, block_frequencies);
            }
        }
    }
    ImGui::End();
}

void RenderDeviceControls(Device& device, const block_frequency_table_t& block_frequencies) {
    std::string preview_label;
    if (!device.GetIsGainManual()) {
        preview_label = "Automatic";
    } else {
        preview_label = fmt::format("{:.1f}dB", device.GetSelectedGain());
    }

    if (ImGui::BeginCombo("Gains", preview_label.c_str())) {
        if (ImGui::Selectable("Automatic", !device.GetIsGainManual())) {
            device.SetAutoGain();
        }
        for (auto gain: device.GetGainList()) {
            const auto label_str = fmt::format("{:.1f}dB", gain);
            const bool is_selected = 
                device.GetIsGainManual() && (device.GetSelectedGain() == gain);
            if (ImGui::Selectable(label_str.c_str(), is_selected)) {
                device.SetGain(gain);
            }
            if (is_selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    preview_label = fmt::format("{}:\t{:.3f}", 
        device.GetSelectedFrequencyLabel(), 
        static_cast<float>(device.GetSelectedFrequency())*1e-6f);

    if (ImGui::BeginCombo("Frequencies", preview_label.c_str())) {
        for (auto& [label, value]: block_frequencies) {
            const bool is_selected = (value == device.GetSelectedFrequency());
            const auto label_str = fmt::format("{}:\t{:.3f}", 
                label, static_cast<float>(value)*1e-6f);
            if (ImGui::Selectable(label_str.c_str(), is_selected)) {
                device.SetCenterFrequency(label, value);
            }
            if (is_selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    auto& errors = device.GetErrorList();
    if (ImGui::BeginListBox("###Errors")) {
        for (auto& error: errors) {
            ImGui::Selectable(error.c_str());
        }
        ImGui::EndListBox();
    }
}