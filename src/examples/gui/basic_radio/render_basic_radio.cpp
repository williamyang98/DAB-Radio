#include "./render_basic_radio.h"

#include "basic_radio/basic_radio.h"
#include "basic_radio/basic_slideshow.h"

#include <fmt/core.h>
#include "./formatters.h"

#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui_internal.h>
#include <imgui.h>
#include "../font_awesome_definitions.h"
#include "../imgui_extensions.h"
#include "./render_common.h"

void RenderSimple_ServiceList(BasicRadio& radio, BasicRadioViewController& controller);
void RenderSimple_Service(BasicRadio& radio, BasicRadioViewController& controller, Service* service);

void RenderSimple_ServiceComponentList(BasicRadio& radio, BasicRadioViewController& controller, Service* service);
void RenderSimple_ServiceComponent(BasicRadio& radio, BasicRadioViewController& controller, ServiceComponent& component);

void RenderSimple_Basic_DAB_Plus_Channel(BasicRadio& radio, BasicRadioViewController& controller, Basic_DAB_Plus_Channel& channel, const subchannel_id_t subchannel_id);
void RenderSimple_BasicSlideshowSelected(BasicRadio& radio, BasicRadioViewController& controller);

void RenderSimple_LinkServices(BasicRadio& radio, BasicRadioViewController& controller, Service* service);
void RenderSimple_LinkService(BasicRadio& radio, BasicRadioViewController& controller, LinkService& link_service);
void RenderSimple_GlobalBasicAudioChannelControls(BasicRadio& radio);

// Render a list of the services
void RenderBasicRadio(BasicRadio& radio, BasicRadioViewController& controller) {
    auto& db_manager = radio.GetDatabaseManager();
    auto& db = db_manager.GetDatabase();
    auto lock = std::scoped_lock(db_manager.GetDatabaseMutex());

    auto* selected_service = db.GetService(controller.selected_service);

    RenderSimple_ServiceList(radio, controller);
    RenderSimple_Service(radio, controller, selected_service);

    RenderEnsemble(radio);
    RenderDateTime(radio);
    RenderDatabaseStatistics(radio);

    RenderSimple_BasicSlideshowSelected(radio, controller);
    RenderSimple_GlobalBasicAudioChannelControls(radio);
    RenderOtherEnsembles(radio);
    RenderSimple_LinkServices(radio, controller, selected_service);
    RenderSimple_ServiceComponentList(radio, controller, selected_service);
}

void RenderSimple_ServiceList(BasicRadio& radio, BasicRadioViewController& controller) {
    auto& db = radio.GetDatabaseManager().GetDatabase();
    const auto window_title = fmt::format("Services ({})###Services panel", db.services.size());
    if (ImGui::Begin(window_title.c_str())) {
        auto& search_filter = controller.services_filter;
        search_filter.Draw("###Services search filter", -1.0f);
        if (ImGui::BeginListBox("###Services list", ImVec2(-1,-1))) {
            for (auto& service: db.services) {
                if (!search_filter.PassFilter(service.label.c_str())) {
                    continue;
                }
                const int service_id = static_cast<int>(service.reference);
                const bool is_selected = (service_id == controller.selected_service);
                auto label = fmt::format("{}###{}", service.label, service.reference);
                if (ImGui::Selectable(label.c_str(), is_selected)) {
                    controller.selected_service = is_selected ? -1 : service_id;
                }

                // Get status information
                bool is_play_audio   = false;
                bool is_decode_audio = false;
                bool is_decode_data  = false;
                auto* components = db.GetServiceComponents(service.reference);
                if (components) {
                    for (auto component: *components) {
                        auto* channel = radio.Get_DAB_Plus_Channel(component->subchannel_id);
                        if (channel) {
                            auto& controls = channel->GetControls();
                            if (controls.GetIsPlayAudio())   is_play_audio   = true;
                            if (controls.GetIsDecodeAudio()) is_decode_audio = true;
                            if (controls.GetIsDecodeData())  is_decode_data  = true;
                        }
                    }
                }
                auto status_str = fmt::format("{} {} {} ", 
                    is_play_audio   ? ICON_FA_VOLUME_UP : "",
                    is_decode_audio ? ICON_FA_MUSIC     : "",
                    is_decode_data  ? ICON_FA_DOWNLOAD  : ""
                );

                float offset = ImGui::GetWindowWidth() - ImGui::CalcTextSize(status_str.c_str()).x;
                ImGui::SameLine(offset);
                ImGui::Text("%.*s", status_str.length(), status_str.c_str());
            }
            ImGui::EndListBox();
        }
    }
    ImGui::End();
}

void RenderSimple_Service(BasicRadio& radio, BasicRadioViewController& controller, Service* service) {
    auto& db = radio.GetDatabaseManager().GetDatabase();
    auto& ensemble = *db.GetEnsemble();

    if (ImGui::Begin("Service Description") && service) {
        ImGuiTableFlags flags = ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Hideable | ImGuiTableFlags_Borders;
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

            FIELD_MACRO("Name", "%.*s", service->label.length(), service->label.c_str());
            FIELD_MACRO("ID", "%u", service->reference);
            FIELD_MACRO("Country Code", "0x%02X.%01X", service->extended_country_code, service->country_id);
            FIELD_MACRO("Programme Type", "%s (%u)", 
                GetProgrammeTypeString(ensemble.international_table_id, service->programme_type),
                service->programme_type);
            FIELD_MACRO("Language", "%s (%u)", GetLanguageTypeString(service->language), service->language);
            FIELD_MACRO("Closed Caption", "%u", service->closed_caption);
            // FIELD_MACRO("Country Name", "%s", GetCountryString(
            //     service->extended_country_code ? service->extended_country_code : ensemble.extended_country_code, 
            //     service->country_id ? service->country_id : ensemble.country_id));

            #undef FIELD_MACRO

            ImGui::EndTable();
        }
    }
    ImGui::End();
}

void RenderSimple_ServiceComponentList(BasicRadio& radio, BasicRadioViewController& controller, Service* service) {
    auto& db = radio.GetDatabaseManager().GetDatabase();

    // Render the service components along with their associated subchannel
    auto* components = service ? db.GetServiceComponents(service->reference) : NULL;
    const auto window_label = fmt::format("Service Components ({})###Service Components Panel",
        components ? components->size() : 0);
    if (ImGui::Begin(window_label.c_str()) && components) {
        for (auto component: *components) {
            RenderSimple_ServiceComponent(radio, controller, *component);
        }
    }
    ImGui::End();
}

void RenderSimple_ServiceComponent(BasicRadio& radio, BasicRadioViewController& controller, ServiceComponent& component) {
    auto& db = radio.GetDatabaseManager().GetDatabase();
    const auto subchannel_id = component.subchannel_id;
    auto* subchannel = db.GetSubchannel(subchannel_id);

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
            
            FIELD_MACRO("Label", "%.*s", component.label.length(), component.label.c_str());
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
        if ((subchannel != NULL) && ImGui::BeginTable("Subchannel", 2, table_flags)) {
            ImGui::TableSetupColumn("Field", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();

            int row_id  = 0;
            const auto prot_label = GetSubchannelProtectionLabel(*subchannel);
            const uint32_t bitrate_kbps = GetSubchannelBitrate(*subchannel);
            FIELD_MACRO("Subchannel ID", "%u", subchannel->id);
            FIELD_MACRO("Start Address", "%u", subchannel->start_address);
            FIELD_MACRO("Capacity Units", "%u", subchannel->length);
            FIELD_MACRO("Protection", "%.*s", prot_label.length(), prot_label.c_str());
            FIELD_MACRO("Bitrate", "%u kb/s", bitrate_kbps);

            ImGui::EndTable();
        }
    }
    ImGui::End();

    #undef FIELD_MACRO

    auto* dab_plus_channel = radio.Get_DAB_Plus_Channel(subchannel_id);
    if (dab_plus_channel != NULL) {
        if (ImGui::Begin("DAB Plus Channel")) {
            RenderSimple_Basic_DAB_Plus_Channel(radio, controller, *dab_plus_channel, subchannel_id);
        }
        ImGui::End();
    }
}

void RenderSimple_Basic_DAB_Plus_Channel(BasicRadio& radio, BasicRadioViewController& controller, Basic_DAB_Plus_Channel& channel, subchannel_id_t subchannel_id) {
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

    static const auto ERROR_INDICATOR = [](const char* label, bool is_error) {
        static const auto COLOR_NO_ERROR = ImColor(0,255,0).Value;
        static const auto COLOR_ERROR    = ImColor(255,0,0).Value;
        const auto padding = ImGui::GetStyle().FramePadding / 2;
        const auto pos_group_start = ImGui::GetCursorScreenPos();
        ImGui::BeginGroup();
        ImGui::PushStyleColor(ImGuiCol_Text, is_error ? COLOR_ERROR : COLOR_NO_ERROR);
        ImGui::Text(ICON_FA_CIRCLE);
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::Text(label);
        ImGui::EndGroup();
        const auto pos_group_end = ImGui::GetItemRectMax();
        ImGui::GetWindowDrawList()->AddRect(
            pos_group_start-padding, pos_group_end+padding, 
            ImColor(ImGui::GetStyleColorVec4(ImGuiCol_Border)), 
            ImGui::GetStyle().FrameBorderSize);
    };

    ImGui::SameLine();

    ImGui::BeginGroup();
    ERROR_INDICATOR("Firecode", channel.IsFirecodeError());
    ImGui::SameLine();
    ERROR_INDICATOR("Reed Solomon", channel.IsRSError());
    ImGui::SameLine();
    ERROR_INDICATOR("Access Unit CRC", channel.IsAUError());
    ImGui::SameLine();
    ERROR_INDICATOR("AAC Decoder", channel.IsCodecError());
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

    // Programme associated data
    // 1. Dynamic label
    // 2. MOT slideshow
    auto& label = channel.GetDynamicLabel();
    ImGui::Text("Dynamic label: %.*s", label.length(), label.c_str());

    auto& slideshow_manager = channel.GetSlideshowManager();
    auto lock = std::scoped_lock(slideshow_manager.GetMutex());
    auto& slideshows = slideshow_manager.GetSlideshows();

    ImGuiWindowFlags window_flags = ImGuiWindowFlags_AlwaysAutoResize;
    if (ImGui::BeginChild("Slideshow", ImVec2(0, 0), true, window_flags)) {
        ImGuiStyle& style = ImGui::GetStyle();
        const float window_width = ImGui::GetWindowContentRegionMax().x;
        float curr_x = 0.0f;
        int slideshow_id = 0;

        for (auto& slideshow: slideshows) {
            auto* texture = controller.AddTexture(subchannel_id, slideshow.transport_id, slideshow.image_data);
            if (texture == NULL) {
                continue;
            }

            // Determine size of thumbnail
            const auto texture_id = reinterpret_cast<ImTextureID>(texture->GetTextureID());
            const float target_height = 200.0f;
            const float scale = target_height / static_cast<float>(texture->GetHeight());
            const auto texture_size = ImVec2(
                static_cast<float>(texture->GetWidth()) * scale, 
                static_cast<float>(texture->GetHeight()) * scale
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
                ImGui::SetTooltip("%.*s", slideshow.name.length(), slideshow.name.c_str());
            }
            if (ImGui::IsItemClicked()) {
                controller.SetSelectedSlideshow({ subchannel_id, &slideshow });
            }
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
}

void RenderSimple_BasicSlideshowSelected(BasicRadio& radio, BasicRadioViewController& controller) {
    auto selection = controller.GetSelectedSlideshow();
    auto* slideshow = selection.slideshow;
    if (slideshow == NULL) {
        return;
    }

    auto* texture = controller.GetTexture(selection.subchannel_id, slideshow->transport_id);

    bool is_open = true;
    if (ImGui::Begin("Slideshow Viewer", &is_open)) {
        auto dockspace_id = ImGui::GetID("Slideshow viewer dockspace");
        ImGui::DockSpace(dockspace_id);

        ImGuiWindowFlags image_flags = ImGuiWindowFlags_HorizontalScrollbar;
        if (ImGui::Begin("Image Viewer", NULL, image_flags)) {
            if (texture != NULL) {
                const auto texture_id = reinterpret_cast<ImTextureID>(texture->GetTextureID());
                const auto texture_size = ImVec2(
                    static_cast<float>(texture->GetWidth()), 
                    static_cast<float>(texture->GetHeight())
                );
                ImGui::Image(texture_id, texture_size);
            }
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
                FIELD_MACRO("Name", "%.*s", slideshow->name.length(), slideshow->name.c_str());
                FIELD_MACRO("Trigger Time", "%llu", slideshow->trigger_time);
                FIELD_MACRO("Expire Time", "%llu", slideshow->expire_time);
                FIELD_MACRO("Category ID", "%u", slideshow->category_id);
                FIELD_MACRO("Slide ID", "%u", slideshow->slide_id);
                FIELD_MACRO("Category title", "%.*s", slideshow->category_title.length(), slideshow->category_title.c_str());
                FIELD_MACRO("Click Through URL", "%.*s", slideshow->click_through_url.length(), slideshow->click_through_url.c_str());
                FIELD_MACRO("Alt Location URL", "%.*s", slideshow->alt_location_url.length(), slideshow->alt_location_url.c_str());
                FIELD_MACRO("Size", "%zu Bytes", slideshow->image_data.size());

                if (texture != NULL) {
                    FIELD_MACRO("Resolution", "%u x %u", texture->GetWidth(), texture->GetHeight());
                    FIELD_MACRO("Internal Texture ID", "%u", texture->GetTextureID());
                }
                
                #undef FIELD_MACRO
                ImGui::EndTable();
            }
        }
        ImGui::End();
    }
    ImGui::End();

    if (!is_open) {
        controller.SetSelectedSlideshow({0,NULL});
    }
}

void RenderSimple_LinkServices(BasicRadio& radio, BasicRadioViewController& controller, Service* service) {
    auto& db = radio.GetDatabaseManager().GetDatabase();

    auto* linked_services = service ? db.GetServiceLSNs(service->reference) : NULL;
    const size_t nb_linked_services = linked_services ? linked_services->size() : 0;
    auto window_label = fmt::format("Linked Services ({})###Linked Services", nb_linked_services);

    if (ImGui::Begin(window_label.c_str()) && linked_services) {
        const ImGuiTableFlags flags = ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Hideable | ImGuiTableFlags_Borders;
        for (auto* linked_service: *linked_services) {
            RenderSimple_LinkService(radio, controller, *linked_service);
        }
    }
    ImGui::End();
}

void RenderSimple_LinkService(BasicRadio& radio, BasicRadioViewController& controller, LinkService& link_service) {
    auto& db = radio.GetDatabaseManager().GetDatabase();
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
        auto* fm_services = db.Get_LSN_FM_Services(link_service.id);
        if (fm_services) {
            const auto fm_label = fmt::format("FM Services ({})###FM Services", fm_services->size());
            if (ImGui::CollapsingHeader(fm_label.c_str(), ImGuiTreeNodeFlags_None)) {
                if (ImGui::BeginTable("FM Table", 3, flags)) {
                    ImGui::TableSetupColumn("Callsign",         ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableSetupColumn("Time compensated", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableSetupColumn("Frequencies",      ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableHeadersRow();

                    int row_id  = 0;
                    for (auto& fm_service: *fm_services) {
                        ImGui::PushID(row_id++);
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::TextWrapped("%04X", fm_service->RDS_PI_code);
                        ImGui::TableSetColumnIndex(1);
                        ImGui::TextWrapped("%s", fm_service->is_time_compensated ? "Yes" : "No");
                        ImGui::TableSetColumnIndex(2);
                        auto& frequencies = fm_service->frequencies;
                        for (auto& freq: frequencies) {
                            ImGui::Text("%3.3f MHz", static_cast<float>(freq)*1e-6f);
                        }
                        ImGui::PopID();
                    }
                    ImGui::EndTable();
                }
            }
        }

        // DRM Services
        auto* drm_services = db.Get_LSN_DRM_Services(link_service.id);
        if (drm_services != NULL) {
            const auto drm_label = fmt::format("DRM Services ({})###DRM Services", drm_services->size());
            if (ImGui::CollapsingHeader(drm_label.c_str())) {
                if (ImGui::BeginTable("DRM Table", 3, flags)) {
                    ImGui::TableSetupColumn("ID",               ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableSetupColumn("Time compensated", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableSetupColumn("Frequencies",      ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableHeadersRow();

                    int row_id  = 0;
                    for (auto* drm_service: *drm_services) {
                        ImGui::PushID(row_id++);
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::TextWrapped("%u", drm_service->drm_code);
                        ImGui::TableSetColumnIndex(1);
                        ImGui::TextWrapped("%s", drm_service->is_time_compensated ? "Yes" : "No");
                        ImGui::TableSetColumnIndex(2);
                        auto& frequencies = drm_service->frequencies;
                        for (auto& freq: frequencies) {
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
    auto& db = radio.GetDatabaseManager().GetDatabase();
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
        auto* channel = radio.Get_DAB_Plus_Channel(subchannel.id);
        if (channel == NULL) continue;

        auto& control = channel->GetControls();
        control.SetIsDecodeAudio(decode_audio);
        control.SetIsDecodeData(decode_data);
        control.SetIsPlayAudio(play_audio);
    }
}