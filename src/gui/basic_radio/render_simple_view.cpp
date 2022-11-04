#include "render_simple_view.h"
#include "basic_radio/basic_radio.h"

#include "imgui.h"
#include <fmt/core.h>
#include "formatters.h"

#include "render_common.h"

void RenderSimple_ServiceList(BasicRadio* radio, SimpleViewController* controller);
void RenderSimple_Service(BasicRadio* radio, SimpleViewController* controller);
void RenderSimple_ServiceComponentList(BasicRadio* radio, SimpleViewController* controller);
void RenderSimple_ServiceComponent(BasicRadio* radio, SimpleViewController* controller);
void RenderSimple_LinkServices(BasicRadio* radio, SimpleViewController* controller);
void RenderSimple_LinkService(BasicRadio* radio, SimpleViewController* controller, LinkService* link_service);

// Render a list of the services
void RenderSimple_Root(BasicRadio* radio, SimpleViewController* controller) {
    auto db = radio->GetDatabaseManager()->GetDatabase();
    if (ImGui::Begin("Simple View")) 
    {
        ImGuiID dockspace_id = ImGui::GetID("Simple View Dockspace");
        ImGui::DockSpace(dockspace_id);

        RenderSimple_ServiceList(radio, controller);
        RenderSimple_Service(radio, controller);

        RenderEnsemble(radio);
        RenderDateTime(radio);
        RenderDatabaseStatistics(radio);

        RenderOtherEnsembles(radio);
        RenderSimple_LinkServices(radio, controller);
        RenderSimple_ServiceComponentList(radio, controller);
    }
    ImGui::End();
}

void RenderSimple_ServiceList(BasicRadio* radio, SimpleViewController* controller) {
    auto db = radio->GetDatabaseManager()->GetDatabase();
    const auto window_title = fmt::format("Services ({})###Services panel", db->services.size());
    if (ImGui::Begin(window_title.c_str())) {
        auto& search_filter = controller->services_filter;
        search_filter.Draw("###Services search filter", -1.0f);
        if (ImGui::BeginListBox("###Services list", ImVec2(-1,-1))) {
            for (auto& service: db->services) {
                if (!search_filter.PassFilter(service.label.c_str())) {
                    continue;
                }
                const int service_id = static_cast<int>(service.reference);
                const bool is_selected = (service_id == controller->selected_service);
                auto label = fmt::format("{}###{}", service.label, service.reference);
                if (ImGui::Selectable(label.c_str(), is_selected)) {
                    controller->selected_service = is_selected ? -1 : service_id;
                }
            }
            ImGui::EndListBox();
        }
    }
    ImGui::End();
}

void RenderSimple_Service(BasicRadio* radio, SimpleViewController* controller) {
    auto* db = radio->GetDatabaseManager()->GetDatabase();
    const auto selected_service_id = controller->selected_service;
    auto* service = (selected_service_id == -1) ? NULL : db->GetService(selected_service_id);

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
                ImGui::TextWrapped(fmt, ##__VA_ARGS__);\
                ImGui::PopID();\
            }\

            FIELD_MACRO("Name", "%.*s", service->label.length(), service->label.c_str());
            FIELD_MACRO("ID", "%u", service->reference);
            FIELD_MACRO("Country ID", "%u", service->country_id);
            FIELD_MACRO("Extended Country Code", "0x%02X", service->extended_country_code);
            FIELD_MACRO("Programme Type", "%u", service->programme_type);
            FIELD_MACRO("Language", "%u", service->language);
            FIELD_MACRO("Closed Caption", "%u", service->closed_caption);

            #undef FIELD_MACRO

            ImGui::EndTable();
        }
    }
    ImGui::End();
}

void RenderSimple_ServiceComponentList(BasicRadio* radio, SimpleViewController* controller) {
    auto* db = radio->GetDatabaseManager()->GetDatabase();
    const auto selected_service_id = controller->selected_service;
    auto* service = (selected_service_id == -1) ? NULL : db->GetService(selected_service_id);

    // Render the service components along with their associated subchannel
    auto* components = service ? db->GetServiceComponents(service->reference) : NULL;
    const auto window_label = fmt::format("Service Components ({})###Service Components Panel",
        components ? components->size() : 0);
    if (ImGui::Begin(window_label.c_str()) && components) {
        ImGuiTableFlags flags = ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Hideable | ImGuiTableFlags_Borders;
        if (ImGui::BeginTable("Components table", 6, flags)) 
        {
            ImGui::TableSetupColumn("Label",            ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Component ID",     ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Global ID",        ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Subchannel ID",    ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Transport Mode",   ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Type",             ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();

            int row_id  = 0;
            for (auto& component: *components) {
                ImGui::PushID(row_id++);

                const bool is_audio_type = (component->transport_mode == TransportMode::STREAM_MODE_AUDIO);
                const char* type_str = is_audio_type ? 
                    GetAudioTypeString(component->audio_service_type) :
                    GetDataTypeString(component->data_service_type);

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextWrapped("%.*s", component->label.length(), component->label.c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::TextWrapped("%u", component->component_id);
                ImGui::TableSetColumnIndex(2);
                ImGui::TextWrapped("%u", component->global_id);
                ImGui::TableSetColumnIndex(3);
                ImGui::TextWrapped("%u", component->subchannel_id);
                ImGui::TableSetColumnIndex(4);
                ImGui::TextWrapped("%s", GetTransportModeString(component->transport_mode));
                ImGui::TableSetColumnIndex(5);
                ImGui::TextWrapped("%s", type_str);

                auto* player = radio->GetAudioChannel(component->subchannel_id);
                if (player != NULL) {
                    auto& controls = player->GetControls();
                    const bool is_selected = controls.GetAllEnabled();
                    ImGui::SameLine();
                    if (ImGui::Selectable("###select_button", is_selected, ImGuiSelectableFlags_SpanAllColumns)) {
                        if (is_selected) {
                            controls.StopAll();
                        } else {
                            controls.RunAll();
                        }
                    }
                }

                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    }
    ImGui::End();
}

void RenderSimple_ServiceComponent(BasicRadio* radio, SimpleViewController* controller) {

}

void RenderSimple_LinkServices(BasicRadio* radio, SimpleViewController* controller) {
    auto* db = radio->GetDatabaseManager()->GetDatabase();
    const auto selected_service_id = controller->selected_service;
    auto* service = (selected_service_id == -1) ? NULL : db->GetService(selected_service_id);

    auto* linked_services = service ? db->GetServiceLSNs(service->reference) : NULL;
    const size_t nb_linked_services = linked_services ? linked_services->size() : 0;
    auto window_label = fmt::format("Linked Services ({})###Linked Services", nb_linked_services);

    if (ImGui::Begin(window_label.c_str()) && linked_services) {
        const ImGuiTableFlags flags = ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Hideable | ImGuiTableFlags_Borders;
        for (auto& linked_service: *linked_services) {
            RenderSimple_LinkService(radio, controller, linked_service);
        }
    }
    ImGui::End();
}

void RenderSimple_LinkService(BasicRadio* radio, SimpleViewController* controller, LinkService* link_service) {
    auto db = radio->GetDatabaseManager()->GetDatabase();
    auto label = fmt::format("###lsn_{}", link_service->id);

    #define FIELD_MACRO(name, fmt, ...) {\
        ImGui::PushID(row_id++);\
        ImGui::TableNextRow();\
        ImGui::TableSetColumnIndex(0);\
        ImGui::TextWrapped(name);\
        ImGui::TableSetColumnIndex(1);\
        ImGui::TextWrapped(fmt, ##__VA_ARGS__);\
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
            FIELD_MACRO("LSN", "%u", link_service->id);
            FIELD_MACRO("Active", "%s", link_service->is_active_link ? "Yes" : "No");
            FIELD_MACRO("Hard Link", "%s", link_service->is_hard_link ? "Yes": "No");
            FIELD_MACRO("International", "%s", link_service->is_international ? "Yes" : "No");
            ImGui::EndTable();
        }

        // FM Services
        auto* fm_services = db->Get_LSN_FM_Services(link_service->id);
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
        auto* drm_services = db->Get_LSN_DRM_Services(link_service->id);
        if (drm_services) {
            const auto drm_label = fmt::format("DRM Services ({})###DRM Services", drm_services->size());
            if (ImGui::CollapsingHeader(drm_label.c_str())) {
                if (ImGui::BeginTable("DRM Table", 3, flags)) {
                    ImGui::TableSetupColumn("ID",               ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableSetupColumn("Time compensated", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableSetupColumn("Frequencies",      ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableHeadersRow();

                    int row_id  = 0;
                    for (auto& drm_service: *drm_services) {
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