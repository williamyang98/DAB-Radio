#include "./render_devices.h"

#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>
#include <fmt/core.h>
#include <optional>
#include "device/device.h"
#include "device/device_list.h"

std::shared_ptr<Device> RenderDeviceList(DeviceList& device_list, Device* device) {
    if (ImGui::Button("Refresh")) {
        device_list.refresh();
    }

    std::string preview_label;
    if (device == nullptr) {
        preview_label = "None selected";
    } else {
        preview_label = device->GetDescriptor().product;
        if (preview_label.empty()) {
            preview_label = "[Unknown Name]";
        }
    }

    std::optional<size_t> selected_index = std::nullopt;
    {
        auto lock = std::unique_lock(device_list.get_mutex_descriptors());
        auto descriptors = device_list.get_descriptors();
        auto label_selected = fmt::format("Devices ({})###Devices", descriptors.size());
        if (descriptors.size() == 0) ImGui::BeginDisabled();
        if (ImGui::BeginCombo(label_selected.c_str(), preview_label.c_str())) {
            for (size_t i = 0; i < descriptors.size(); i++) {
                const auto& descriptor = descriptors[i];
                std::string_view vendor = descriptor.vendor;
                std::string_view product = descriptor.product;
                std::string_view serial = descriptor.serial;
                if (vendor.empty()) vendor = "?";
                if (product.empty()) product = "?";
                if (serial.empty()) serial = "?";
                auto label = fmt::format("Vendor={} Product={} Serial={}", vendor, product, serial);
                if (ImGui::Selectable(label.c_str(), false)) {
                    selected_index = std::optional(i);
                }
            }
            ImGui::EndCombo();
        }
        if (descriptors.size() == 0) ImGui::EndDisabled();
    }
    if (selected_index.has_value()) {
        return device_list.get_device(selected_index.value()); 
    }
    return nullptr;
}

void RenderDevice(Device& device, const block_frequency_table_t& frequencies) {
    std::string preview_label;
    if (!device.GetIsGainManual()) {
        preview_label = "Automatic";
    } else {
        preview_label = fmt::format("{:.1f}dB", device.GetSelectedGain());
    }

    auto& gains = device.GetGainList();
    const auto curr_gain = device.GetSelectedGain();
    int selected_index = -1;

    if (device.GetIsGainManual()) {
        for (size_t i = 0; i < gains.size(); i++) {
            if (gains[i] == curr_gain) {
                selected_index = int(i);
                break;
            }
        }
    }

    if (ImGui::SliderInt("Gain", &selected_index, -1, int(gains.size())-1, preview_label.c_str())) {
        if (selected_index == -1) {
            device.SetAutoGain();
        } else {
            auto gain = gains[selected_index];
            device.SetGain(gain);
        }
    }
 
    const uint32_t selected_frequency_Hz = device.GetSelectedFrequency();
    const float selected_frequency_MHz = float(selected_frequency_Hz)*1e-6f;
    preview_label = fmt::format("{}:\t{:.3f}", device.GetSelectedFrequencyLabel(), selected_frequency_MHz);
    if (ImGui::BeginCombo("Frequencies", preview_label.c_str())) {
        for (const auto& [channel, frequency_Hz]: frequencies) {
            const float frequency_MHz = float(frequency_Hz)*1e-6f;
            const bool is_selected = (frequency_Hz == selected_frequency_Hz);
            const auto label_str = fmt::format("{}:\t{:.3f}", channel, frequency_MHz);
            if (ImGui::Selectable(label_str.c_str(), is_selected)) {
                device.SetCenterFrequency(channel, frequency_Hz);
            }
            if (is_selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    auto& errors = device.GetErrorList();
    auto error_title = fmt::format("Errors ({})###Errors", errors.size());
    if (ImGui::BeginListBox(error_title.c_str())) {
        for (auto& error: errors) {
            ImGui::Selectable(error.c_str());
        }
        ImGui::EndListBox();
    }
}