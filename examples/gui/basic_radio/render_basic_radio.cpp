#include "./render_common.h"
#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>
#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <algorithm>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>
#include <fmt/format.h>
#include "basic_radio/basic_audio_channel.h"
#include "basic_radio/basic_dab_channel.h"
#include "basic_radio/basic_dab_plus_channel.h"
#include "basic_radio/basic_data_packet_channel.h"
#include "basic_radio/basic_radio.h"
#include "basic_radio/basic_slideshow.h"
#include "dab/database/dab_database.h"
#include "dab/database/dab_database_entities.h"
#include "dab/database/dab_database_types.h"
#include "../font_awesome_definitions.h"
#include "./basic_radio_view_controller.h"
#include "./formatters.h"
#include "./render_basic_radio.h"

template <typename T, typename F>
static T* find_by_callback(std::vector<T>& vec, F&& func) {
    for (auto& e: vec) {
        if (func(e)) return &e;
    }
    return nullptr;
}

static void RenderSimple_ServiceList(BasicRadio& radio, BasicRadioViewController& controller);
static void RenderSimple_Service(BasicRadio& radio, BasicRadioViewController& controller, const Service* service);
static void RenderSimple_ServiceComponentList(BasicRadio& radio, BasicRadioViewController& controller, const Service* service);
static void RenderSimple_ServiceComponent(BasicRadio& radio, BasicRadioViewController& controller, ServiceComponent& component);
static void RenderSimple_Basic_Audio_Channel(BasicRadio& radio, BasicRadioViewController& controller, Basic_Audio_Channel& channel, const subchannel_id_t subchannel_id);
static void RenderSimple_Basic_Data_Channel(BasicRadio& radio, BasicRadioViewController& controller, Basic_Data_Packet_Channel& channel, const subchannel_id_t subchannel_id);
static void RenderSimple_BasicSlideshowSelected(BasicRadio& radio, BasicRadioViewController& controller);
static void RenderSimple_LinkServices(BasicRadio& radio, BasicRadioViewController& controller, const Service* service);
static void RenderSimple_LinkService(BasicRadio& radio, BasicRadioViewController& controller, const LinkService& link_service);
static void RenderSimple_GlobalBasicAudioChannelControls(BasicRadio& radio);
static void RenderSimple_Basic_DAB_Plus_Channel_Status(Basic_DAB_Plus_Channel& channel);
static void RenderSimple_Basic_DAB_Channel_Status(Basic_DAB_Channel& channel);

void RenderBasicRadio(BasicRadio& radio, BasicRadioViewController& controller) {
    auto lock = std::scoped_lock(radio.GetMutex());
    auto& db = radio.GetDatabase();

    auto* selected_service = find_by_callback(
        db.services,
        [&controller](const auto& service) {
            return service.reference == controller.selected_service;
        }
    );

    RenderSimple_ServiceList(radio, controller);
    RenderSimple_Service(radio, controller, selected_service);

    RenderOtherEnsembles(radio);
    RenderEnsemble(radio);
    RenderDateTime(radio);
    RenderDatabaseStatistics(radio);

    RenderSimple_BasicSlideshowSelected(radio, controller);
    RenderSimple_GlobalBasicAudioChannelControls(radio);
    RenderSimple_LinkServices(radio, controller, selected_service);
    RenderSimple_ServiceComponentList(radio, controller, selected_service);
}

void RenderSimple_ServiceList(BasicRadio& radio, BasicRadioViewController& controller) {
    auto& db = radio.GetDatabase();
    const auto window_title = fmt::format("Services ({})###Services panel", db.services.size());
    if (ImGui::Begin(window_title.c_str())) {
        auto& search_filter = *(controller.services_filter.get());
        search_filter.Draw("###Services search filter", -1.0f);
        if (ImGui::BeginListBox("###Services list", ImVec2(-1,-1))) {
            static std::vector<Service*> service_list;
            service_list.clear();
            for (auto& service: db.services) {
                if (!search_filter.PassFilter(service.label.c_str())) {
                    continue;
                }
                service_list.push_back(&service);
            }

            std::sort(service_list.begin(), service_list.end(), [](const auto* a, const auto* b) {
                return (a->label.compare(b->label) < 0);
            });

            for (auto* service_ptr: service_list) {
                auto& service = *service_ptr;
                const service_id_t service_id = service.reference;
                const bool is_selected = (service_id == controller.selected_service);
                auto label = fmt::format("{}###{}", service.label.empty() ? "[Unknown]" : service.label, service.reference);
                if (ImGui::Selectable(label.c_str(), is_selected)) {
                    controller.selected_service = is_selected ? -1 : service_id;
                }

                // Get status information
                bool is_play_audio   = false;
                bool is_decode_audio = false;
                bool is_decode_data  = false;
                for (auto& component: db.service_components) {
                    if (component.service_reference != service.reference) continue;
                    auto* channel = radio.Get_Audio_Channel(component.subchannel_id);
                    if (channel) {
                        auto& controls = channel->GetControls();
                        if (controls.GetIsPlayAudio())   is_play_audio   = true;
                        if (controls.GetIsDecodeAudio()) is_decode_audio = true;
                        if (controls.GetIsDecodeData())  is_decode_data  = true;
                    }
                }
                auto status_str = fmt::format("{} {} {} ", 
                    is_play_audio   ? ICON_FA_VOLUME_UP : "",
                    is_decode_audio ? ICON_FA_MUSIC     : "",
                    is_decode_data  ? ICON_FA_DOWNLOAD  : ""
                );

                const float offset = ImGui::GetWindowWidth() - ImGui::CalcTextSize(status_str.c_str()).x;
                ImGui::SameLine(offset);
                ImGui::Text("%.*s", int(status_str.length()), status_str.c_str());
            }
            ImGui::EndListBox();
        }
    }
    ImGui::End();
}

void RenderSimple_Service(BasicRadio& radio, BasicRadioViewController& controller, const Service* service) {
    if (ImGui::Begin("Service Description") && service) {
        const ImGuiTableFlags flags = ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Hideable | ImGuiTableFlags_Borders;
        if (ImGui::BeginTable("Service Description", 2, flags)) {
            ImGui::TableSetupColumn("Field", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();

            int row_id = 0;
            #define FIELD_MACRO(name, fmt, ...) {\
                ImGui::PushID(row_id++);\
                ImGui::TableNextRow();\
                ImGui::TableSetColumnIndex(0);\
                ImGui::TextWrapped(name);\
                ImGui::TableSetColumnIndex(1);\
                ImGui::TextWrapped(fmt, __VA_ARGS__);\
                ImGui::PopID();\
            }\

            const auto& db = radio.GetDatabase();
            const auto& ensemble = db.ensemble;
            FIELD_MACRO("Name", "%.*s", int(service->label.length()), service->label.c_str());
            FIELD_MACRO("ID", "%u", service->reference);
            {
                extended_country_id_t extended_country_code = (service->extended_country_code != 0) ? service->extended_country_code : ensemble.extended_country_code;
                extended_country_id_t country_id = (service->country_id != 0) ? service->country_id : ensemble.country_id;
                FIELD_MACRO("Country", "%s (0x%02X.%01X)", GetCountryString(extended_country_code, country_id), extended_country_code, country_id);
            }
            FIELD_MACRO("Programme Type", "%s (%u)", 
                GetProgrammeTypeString(ensemble.international_table_id, service->programme_type),
                service->programme_type);
            FIELD_MACRO("Language", "%s (%u)", GetLanguageTypeString(service->language), service->language);
            FIELD_MACRO("Closed Caption", "%u", service->closed_caption);

            #undef FIELD_MACRO

            ImGui::EndTable();
        }
    }
    ImGui::End();
}

void RenderSimple_ServiceComponentList(BasicRadio& radio, BasicRadioViewController& controller, const Service* service) {
    auto& db = radio.GetDatabase();
    static std::vector<ServiceComponent*> service_components;
    service_components.clear();
    if (service) {
        for (auto& service_component: db.service_components) {
            if (service_component.service_reference != service->reference) continue;
            service_components.push_back(&service_component);
        }
    }
    const auto window_label = fmt::format("Service Components ({})###Service Components Panel", service_components.size());
    if (ImGui::Begin(window_label.c_str())) {
        static int selected_component_index = 0;
        const size_t total_components = service_components.size();
        if (total_components > 1) {
            ImGui::SliderInt("Service Component", &selected_component_index, 0, int(total_components-1));
        }
        if (selected_component_index >= int(total_components)) {
            selected_component_index = 0; 
        }
        if (total_components > 0) {
            auto* service_component = service_components[selected_component_index];
            RenderSimple_ServiceComponent(radio, controller, *service_component);
        }
    }
    ImGui::End();
}

void RenderSimple_ServiceComponent(BasicRadio& radio, BasicRadioViewController& controller, ServiceComponent& component) {
    auto& db = radio.GetDatabase();
    const auto subchannel_id = component.subchannel_id;
    auto* subchannel = find_by_callback(
        db.subchannels,
        [subchannel_id](const auto& other) {
            return other.id == subchannel_id;
        }
    );

    ImGui::DockSpace(ImGui::GetID("Service Component Dockspace"));

    #define FIELD_MACRO(name, fmt, ...) {\
        ImGui::PushID(row_id++);\
        ImGui::TableNextRow();\
        ImGui::TableSetColumnIndex(0);\
        ImGui::TextWrapped(name);\
        ImGui::TableSetColumnIndex(1);\
        ImGui::TextWrapped(fmt, __VA_ARGS__);\
        ImGui::PopID();\
    }\

    ImGuiTableFlags table_flags = ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Hideable | ImGuiTableFlags_Borders;

    if (ImGui::Begin("Service Component")) {
        if (ImGui::BeginTable("Service Component", 2, table_flags)) {
            ImGui::TableSetupColumn("Field", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();

            int row_id  = 0;
            const bool is_audio_type = (component.transport_mode == TransportMode::STREAM_MODE_AUDIO);
            const char* type_str = is_audio_type ? 
                GetAudioTypeString(component.audio_service_type) :
                GetDataTypeString(component.data_service_type);
            
            FIELD_MACRO("Label", "%.*s", int(component.label.length()), component.label.c_str());
            FIELD_MACRO("Component ID", "%u", component.component_id);
            FIELD_MACRO("Global ID", "%u", component.global_id);
            FIELD_MACRO("Transport Mode", "%s", GetTransportModeString(component.transport_mode));
            FIELD_MACRO("Type", "%s", type_str);
            // FIELD_MACRO("Subchannel ID", "%u", component.subchannel_id);

            ImGui::EndTable();
        }
    }
    ImGui::End();

    if (ImGui::Begin("Subchannel")) {
        if ((subchannel != nullptr) && ImGui::BeginTable("Subchannel", 2, table_flags)) {
            ImGui::TableSetupColumn("Field", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();

            int row_id  = 0;
            const auto prot_label = GetSubchannelProtectionLabel(*subchannel);
            const uint32_t bitrate_kbps = GetSubchannelBitrate(*subchannel);
            FIELD_MACRO("Subchannel ID", "%u", subchannel->id);
            FIELD_MACRO("Start Address", "%u", subchannel->start_address);
            FIELD_MACRO("Capacity Units", "%u", subchannel->length);
            FIELD_MACRO("Protection", "%.*s", int(prot_label.length()), prot_label.c_str());
            FIELD_MACRO("Bitrate", "%u kb/s", bitrate_kbps);

            ImGui::EndTable();
        }
    }
    ImGui::End();

    #undef FIELD_MACRO

    auto* audio_channel = radio.Get_Audio_Channel(subchannel_id);
    if (audio_channel != nullptr) {
        const auto ascty = audio_channel->GetType();
        const char* channel_name = "Unknown";
        switch (ascty) {
            case AudioServiceType::DAB_PLUS: channel_name = "DAB+"; break;
            case AudioServiceType::DAB: channel_name = "DAB"; break;
            default: channel_name = "Unknown"; break;
        }
        auto label = fmt::format("{} Channel###Channel", channel_name);
        if (ImGui::Begin(label.c_str())) {
            RenderSimple_Basic_Audio_Channel(radio, controller, *audio_channel, subchannel_id);
        }
        ImGui::End();
        return;
    }

    auto* data_channel = radio.Get_Data_Packet_Channel(subchannel_id);
    if (data_channel != nullptr) {
        if (ImGui::Begin("Data Channel###Channel")) {
            RenderSimple_Basic_Data_Channel(radio, controller, *data_channel, subchannel_id);
        }
        ImGui::End();
        return;
    }
}

static void RenderSimple_Slideshow_Manager(BasicRadioViewController& controller, Basic_Slideshow_Manager& slideshow_manager, subchannel_id_t subchannel_id) {
    ImGuiChildFlags child_flags = ImGuiChildFlags_Border;
    ImGuiWindowFlags window_flags = ImGuiWindowFlags_None;
    if (ImGui::BeginChild("Slideshow", ImVec2(0, 0), child_flags, window_flags)) {
        const ImGuiStyle& style = ImGui::GetStyle();
        static std::vector<std::shared_ptr<Basic_Slideshow>> slideshows;
        {
            auto lock = std::unique_lock(slideshow_manager.GetSlideshowsMutex());
            slideshows.clear();
            for (auto& slideshow: slideshow_manager.GetSlideshows()) {
                slideshows.push_back(slideshow);
            }
        }

        const float window_width = ImGui::GetWindowContentRegionMax().x;
        float curr_x = 0.0f;
        int slideshow_id = 0;
        for (auto& slideshow: slideshows) {
            const auto& texture = controller.GetTexture(subchannel_id, slideshow->transport_id, slideshow->image_data);
            // Determine size of thumbnail
            const auto texture_id = reinterpret_cast<ImTextureID>(texture.GetTextureID());
            const float target_height = 200.0f;
            const float scale = target_height / static_cast<float>(texture.GetHeight());
            const auto texture_size = ImVec2(
                static_cast<float>(texture.GetWidth()) * scale, 
                static_cast<float>(texture.GetHeight()) * scale
            );

            // Determine if the thumbnail needs to be on a new line
            const float next_x = curr_x + style.ItemSpacing.x + texture_size.x;
            const bool is_next_line = next_x > window_width;
            if (is_next_line) {
                curr_x = texture_size.x;
            } else {
                if (slideshow_id != 0) ImGui::SameLine();
                curr_x = next_x;
            }

            ImGui::PushID(slideshow_id++);
            ImGui::Image(texture_id, texture_size);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%.*s", int(slideshow->name.length()), slideshow->name.c_str());
            }
            if (ImGui::IsItemClicked()) {
                controller.selected_slideshow = std::optional<SlideshowView>({ subchannel_id, slideshow });
            }
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
}

void RenderSimple_Basic_Audio_Channel(BasicRadio& radio, BasicRadioViewController& controller, Basic_Audio_Channel& channel, subchannel_id_t subchannel_id) {
    // Channel controls
    auto& controls = channel.GetControls();
    if (ImGui::Button("Run All")) {
        controls.RunAll();
    }
    ImGui::SameLine();
    if (ImGui::Button("Stop All")) {
        controls.StopAll();
    }
    bool v = false;
    v = controls.GetIsDecodeAudio();
    ImGui::SameLine();
    if (ImGui::Checkbox("Decode audio", &v)) {
        controls.SetIsDecodeAudio(v);
    }
    v = controls.GetIsDecodeData();
    ImGui::SameLine();
    if (ImGui::Checkbox("Decode data", &v)) {
        controls.SetIsDecodeData(v);
    }
    v = controls.GetIsPlayAudio();
    ImGui::SameLine();
    if (ImGui::Checkbox("Play audio", &v)) {
        controls.SetIsPlayAudio(v);
    }

    const auto ascty = channel.GetType();
    switch (ascty) {
    case AudioServiceType::DAB_PLUS:
        RenderSimple_Basic_DAB_Plus_Channel_Status(dynamic_cast<Basic_DAB_Plus_Channel&>(channel));
        break;
    case AudioServiceType::DAB:
        RenderSimple_Basic_DAB_Channel_Status(dynamic_cast<Basic_DAB_Channel&>(channel));
        break;
    case AudioServiceType::UNDEFINED:
    default:
        break;
    }

    // Programme associated data
    // 1. Dynamic label
    // 2. MOT slideshow
    auto label = channel.GetDynamicLabel();
    ImGui::Text("Dynamic label: %.*s", int(label.length()), label.data());

    auto& slideshow_manager = channel.GetSlideshowManager();
    RenderSimple_Slideshow_Manager(controller, slideshow_manager, subchannel_id);
}

void RenderSimple_Basic_Data_Channel(BasicRadio& radio, BasicRadioViewController& controller, Basic_Data_Packet_Channel& channel, const subchannel_id_t subchannel_id) {
    auto& slideshow_manager = channel.GetSlideshowManager();
    RenderSimple_Slideshow_Manager(controller, slideshow_manager, subchannel_id);
}

static void render_error_indicator(const char* label, bool is_error) {
    static const auto COLOR_NO_ERROR = ImColor(0,255,0).Value;
    static const auto COLOR_ERROR    = ImColor(255,0,0).Value;
    const auto padding = ImGui::GetStyle().FramePadding / 2;
    const auto pos_group_start = ImGui::GetCursorScreenPos();
    ImGui::BeginGroup();
    ImGui::PushStyleColor(ImGuiCol_Text, is_error ? COLOR_ERROR : COLOR_NO_ERROR);
    ImGui::Text("%s", ICON_FA_CIRCLE);
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::Text("%s", label);
    ImGui::EndGroup();
    const auto pos_group_end = ImGui::GetItemRectMax();
    ImGui::GetWindowDrawList()->AddRect(
        pos_group_start-padding, pos_group_end+padding, 
        ImColor(ImGui::GetStyleColorVec4(ImGuiCol_Border)), 
        ImGui::GetStyle().FrameBorderSize);
};

void RenderSimple_Basic_DAB_Plus_Channel_Status(Basic_DAB_Plus_Channel& channel) {
    ImGui::SameLine();
    ImGui::BeginGroup();
    render_error_indicator("Firecode", channel.IsFirecodeError());
    ImGui::SameLine();
    render_error_indicator("Reed Solomon", channel.IsRSError());
    ImGui::SameLine();
    render_error_indicator("Access Unit CRC", channel.IsAUError());
    ImGui::SameLine();
    render_error_indicator("AAC Decoder", channel.IsCodecError());
    ImGui::EndGroup();

    const auto& superframe_header = channel.GetSuperFrameHeader();
    if (superframe_header.sampling_rate != 0) {
        const char* mpeg_surround = GetMPEGSurroundString(superframe_header.mpeg_surround);
        ImGui::Text("Codec: %uHz %s %s %s", 
            superframe_header.sampling_rate, 
            superframe_header.is_stereo ? "Stereo" : "Mono",  
            GetAACDescriptionString(superframe_header.SBR_flag, superframe_header.PS_flag),
            mpeg_surround ? mpeg_surround : "");
    }
}

void RenderSimple_Basic_DAB_Channel_Status(Basic_DAB_Channel& channel) {
    ImGui::SameLine();
    ImGui::BeginGroup();
    render_error_indicator("MP2 Decoder", channel.GetIsError());
    ImGui::EndGroup();

    const auto& audio_params_opt = channel.GetAudioParams();
    if (audio_params_opt.has_value()) {
        auto& params = audio_params_opt.value();
        ImGui::Text("Codec: %dHz %s %dkb/s MP2", 
            params.sample_rate, 
            params.is_stereo ? "Stereo" : "Mono",  
            params.bitrate_kbps);
    }
}

void RenderSimple_BasicSlideshowSelected(BasicRadio& radio, BasicRadioViewController& controller) {
    if (!controller.selected_slideshow.has_value()) {
        return;
    }
    auto& selection = controller.selected_slideshow.value();
    auto& slideshow = selection.slideshow;
    auto& texture = controller.GetTexture(selection.subchannel_id, slideshow->transport_id, slideshow->image_data);

    bool is_open = true;
    if (ImGui::Begin("Slideshow Viewer", &is_open)) {
        auto dockspace_id = ImGui::GetID("Slideshow viewer dockspace");
        ImGui::DockSpace(dockspace_id);

        ImGuiWindowFlags image_flags = ImGuiWindowFlags_HorizontalScrollbar;
        if (ImGui::Begin("Image Viewer", nullptr, image_flags)) {
            const auto texture_id = reinterpret_cast<ImTextureID>(texture.GetTextureID());
            const auto texture_size = ImVec2(
                static_cast<float>(texture.GetWidth()), 
                static_cast<float>(texture.GetHeight())
            );
            ImGui::Image(texture_id, texture_size);
        }
        ImGui::End();

        if (ImGui::Begin("Description")) {
            ImGuiTableFlags flags = ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Hideable | ImGuiTableFlags_Borders;
            if (ImGui::BeginTable("Component", 2, flags)) {
                ImGui::TableSetupColumn("Field", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableHeadersRow();

                int row_id  = 0;
                #define FIELD_MACRO(name, fmt, ...) {\
                    ImGui::PushID(row_id++);\
                    ImGui::TableNextRow();\
                    ImGui::TableSetColumnIndex(0);\
                    ImGui::TextWrapped(name);\
                    ImGui::TableSetColumnIndex(1);\
                    ImGui::TextWrapped(fmt, __VA_ARGS__);\
                    ImGui::PopID();\
                }\

                FIELD_MACRO("Subchannel ID", "%u", selection.subchannel_id);
                FIELD_MACRO("Transport ID", "%u", slideshow->transport_id);
                FIELD_MACRO("Name", "%.*s", int(slideshow->name.length()), slideshow->name.c_str());
                FIELD_MACRO("Trigger Time", "%" PRIi64, int64_t(slideshow->trigger_time));
                FIELD_MACRO("Expire Time", "%" PRIi64, int64_t(slideshow->expire_time));
                FIELD_MACRO("Category ID", "%u", slideshow->category_id);
                FIELD_MACRO("Slide ID", "%u", slideshow->slide_id);
                FIELD_MACRO("Category title", "%.*s", int(slideshow->category_title.length()), slideshow->category_title.c_str());
                FIELD_MACRO("Click Through URL", "%.*s", int(slideshow->click_through_url.length()), slideshow->click_through_url.c_str());
                FIELD_MACRO("Alt Location URL", "%.*s", int(slideshow->alt_location_url.length()), slideshow->alt_location_url.c_str());
                FIELD_MACRO("Size", "%zu Bytes", slideshow->image_data.size());

                FIELD_MACRO("Resolution", "%u x %u", texture.GetWidth(), texture.GetHeight());
                FIELD_MACRO("Internal Texture ID", "%" PRIuPTR, uintptr_t(texture.GetTextureID()));

                #undef FIELD_MACRO
                ImGui::EndTable();
            }
        }
        ImGui::End();
    }
    ImGui::End();

    if (!is_open) {
        controller.selected_slideshow = std::nullopt;
    }
}

void RenderSimple_LinkServices(BasicRadio& radio, BasicRadioViewController& controller, const Service* service) {
    const auto& db = radio.GetDatabase();
    static std::vector<const LinkService*> link_services;
    link_services.clear();
    if (service) {
        for (const auto& link_service: db.link_services) {
            if (link_service.service_reference != service->reference) continue;
            link_services.push_back(&link_service);
        }
    }

    auto window_label = fmt::format("Linked Services ({})###Linked Services", link_services.size());
    if (ImGui::Begin(window_label.c_str())) {
        for (const auto* link_service: link_services) {
            RenderSimple_LinkService(radio, controller, *link_service);
        }
    }
    ImGui::End();
}

void RenderSimple_LinkService(BasicRadio& radio, BasicRadioViewController& controller, const LinkService& link_service) {
    auto& db = radio.GetDatabase();
    auto label = fmt::format("###lsn_{}", link_service.id);

    #define FIELD_MACRO(name, fmt, ...) {\
        ImGui::PushID(row_id++);\
        ImGui::TableNextRow();\
        ImGui::TableSetColumnIndex(0);\
        ImGui::TextWrapped(name);\
        ImGui::TableSetColumnIndex(1);\
        ImGui::TextWrapped(fmt, __VA_ARGS__);\
        ImGui::PopID();\
    }\

    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 2.0f);
    if (ImGui::BeginChild(label.c_str(), ImVec2(-1, 0))) {
        static ImGuiTableFlags flags = ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Hideable | ImGuiTableFlags_Borders;

        // Description header
        ImGui::Text("Link Service Description");
        if (ImGui::BeginTable("LSN Description", 2, flags)) {
            ImGui::TableSetupColumn("Field", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();
            int row_id = 0;
            FIELD_MACRO("LSN", "%u", link_service.id);
            FIELD_MACRO("Active", "%s", link_service.is_active_link ? "Yes" : "No");
            FIELD_MACRO("Hard Link", "%s", link_service.is_hard_link ? "Yes": "No");
            FIELD_MACRO("International", "%s", link_service.is_international ? "Yes" : "No");
            ImGui::EndTable();
        }

        // FM Services
        static std::vector<FM_Service*> fm_services;
        fm_services.clear();
        for (auto& fm_service: db.fm_services) {
            if (fm_service.linkage_set_number != link_service.id) continue;
            fm_services.push_back(&fm_service);
        }
        if (fm_services.size() > 0) {
            const auto fm_label = fmt::format("FM Services ({})###FM Services", fm_services.size());
            if (ImGui::CollapsingHeader(fm_label.c_str(), ImGuiTreeNodeFlags_None)) {
                if (ImGui::BeginTable("FM Table", 3, flags)) {
                    ImGui::TableSetupColumn("Callsign",         ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableSetupColumn("Time compensated", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableSetupColumn("Frequencies",      ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableHeadersRow();

                    int row_id  = 0;
                    for (const auto& fm_service: fm_services) {
                        ImGui::PushID(row_id++);
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::TextWrapped("%04X", fm_service->RDS_PI_code);
                        ImGui::TableSetColumnIndex(1);
                        ImGui::TextWrapped("%s", fm_service->is_time_compensated ? "Yes" : "No");
                        ImGui::TableSetColumnIndex(2);
                        for (const auto& freq: fm_service->frequencies) {
                            ImGui::Text("%3.3f MHz", static_cast<float>(freq)*1e-6f);
                        }
                        ImGui::PopID();
                    }
                    ImGui::EndTable();
                }
            }
        }

        // DRM Services
        static std::vector<DRM_Service*> drm_services;
        drm_services.clear();
        for (auto& drm_service: db.drm_services) {
            if (drm_service.linkage_set_number != link_service.id) continue;
            drm_services.push_back(&drm_service);
        }
        if (drm_services.size() > 0) {
            const auto drm_label = fmt::format("DRM Services ({})###DRM Services", drm_services.size());
            if (ImGui::CollapsingHeader(drm_label.c_str())) {
                if (ImGui::BeginTable("DRM Table", 3, flags)) {
                    ImGui::TableSetupColumn("ID",               ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableSetupColumn("Time compensated", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableSetupColumn("Frequencies",      ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableHeadersRow();

                    int row_id  = 0;
                    for (const auto& drm_service: drm_services) {
                        ImGui::PushID(row_id++);
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::TextWrapped("%u", drm_service->drm_code);
                        ImGui::TableSetColumnIndex(1);
                        ImGui::TextWrapped("%s", drm_service->is_time_compensated ? "Yes" : "No");
                        ImGui::TableSetColumnIndex(2);
                        for (const auto& freq: drm_service->frequencies) {
                            ImGui::Text("%3.3f MHz", static_cast<float>(freq)*1e-6f);
                        }
                        ImGui::PopID();
                    }
                    ImGui::EndTable();
                }
            }
        }

        // AMSS Services
        if (db.amss_services.size() > 0) {
            const auto amss_label = fmt::format("AMSS Services ({})###AMSS Services", db.amss_services.size());
            if (ImGui::CollapsingHeader(amss_label.c_str())) {
                if (ImGui::BeginTable("AMSS Table", 3, flags)) {
                    ImGui::TableSetupColumn("ID",               ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableSetupColumn("Time compensated", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableSetupColumn("Frequencies",      ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableHeadersRow();

                    int row_id  = 0;
                    for (const auto& amss_service: db.amss_services) {
                        ImGui::PushID(row_id++);
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::TextWrapped("%u", amss_service.amss_code);
                        ImGui::TableSetColumnIndex(1);
                        ImGui::TextWrapped("%s", amss_service.is_time_compensated ? "Yes" : "No");
                        ImGui::TableSetColumnIndex(2);
                        for (const auto& freq: amss_service.frequencies) {
                            ImGui::Text("%3.3f MHz", static_cast<float>(freq)*1e-6f);
                        }
                        ImGui::PopID();
                    }
                    ImGui::EndTable();
                }
            }
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();

    #undef FIELD_MACRO
}

void RenderSimple_GlobalBasicAudioChannelControls(BasicRadio& radio) {
    auto& db = radio.GetDatabase();
    auto& subchannels = db.subchannels;

    static bool decode_audio = true;
    static bool decode_data = true;
    static bool play_audio = false;

    bool is_changed = false;

    if (ImGui::Begin("Global Channel Controls")) {
        if (ImGui::Button("Apply Settings")) {
            is_changed = true;
        }
        ImGui::Checkbox("Decode Audio", &decode_audio);
        ImGui::SameLine();
        ImGui::Checkbox("Decode Data", &decode_data);
        ImGui::SameLine();
        ImGui::Checkbox("Play Audio", &play_audio);
    }
    ImGui::End();

    if (!is_changed) {
        return;
    }

    for (auto& subchannel: subchannels) {
        auto* channel = radio.Get_Audio_Channel(subchannel.id);
        if (channel == nullptr) continue;

        auto& control = channel->GetControls();
        control.SetIsDecodeAudio(decode_audio);
        control.SetIsDecodeData(decode_data);
        control.SetIsPlayAudio(play_audio);
    }
}