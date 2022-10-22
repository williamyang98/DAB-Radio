#include "render_basic_radio.h"
#include "basic_radio.h"

#include "imgui.h"
#include <fmt/core.h>

#include "dab/constants/subchannel_protection_tables.h"

// our global filters
struct {
    ImGuiTextFilter services_filter;
    void ClearAll() {
        services_filter.Clear();
    }
} GlobalFilters;

// Convert to string
std::string GetSubchannelProtectionLabel(Subchannel& subchannel) {
    if (subchannel.is_uep) {
        return fmt::format("UEP {}", subchannel.uep_prot_index);
    }
    const bool is_type_A = (subchannel.eep_type == EEP_Type::TYPE_A);
    const int protection_id = subchannel.eep_prot_level+1;
    return fmt::format("EEP {}-{}", protection_id, is_type_A ? 'A' : 'B');
}

uint32_t GetSubchannelBitrate(Subchannel& subchannel) {
    if (subchannel.is_uep) {
        const auto& descriptor = GetUEPDescriptor(subchannel);
        return descriptor.bitrate;
    }

    return CalculateEEPBitrate(subchannel);
}

const char* GetTransportModeString(const TransportMode transport_mode) {
    switch (transport_mode) {
    case TransportMode::STREAM_MODE_AUDIO:
        return "Stream Audio";
    case TransportMode::STREAM_MODE_DATA:
        return "Stream Data";
    case TransportMode::PACKET_MODE_DATA:
        return "Packet Data";
    default:
        return "Unknown";
    }
}

const char* GetAudioTypeString(const AudioServiceType audio_type) {
    switch (audio_type) {
    case AudioServiceType::DAB:
        return "DAB";
    case AudioServiceType::DAB_PLUS:
        return "DAB+";
    default:
        return "Unknown";
    }
}

const char* GetDataTypeString(const DataServiceType data_type) {
    switch (data_type) {
    case DataServiceType::MOT:
        return "Multimedia Object Type";
    case DataServiceType::MPEG2:
        return "MPEG-II";
    case DataServiceType::TRANSPARENT_CHANNEL:
        return "Transparent";
    case DataServiceType::PROPRIETARY:
        return "Proprietary";
    default:
        return "Unknown";
    }
}

// Full information view
void RenderSubchannels(BasicRadio* radio);
void RenderEnsemble(BasicRadio* radio);
void RenderDateTime(BasicRadio* radio);
void RenderDatabaseStatistics(BasicRadio* radio);
void RenderOtherEnsembles(BasicRadio* radio);

// Simplified user friendly view
struct SimpleController {
    int selected_service = -1;
};
void RenderSimple_Root(BasicRadio* radio, SimpleController* controller);
void RenderSimple_ServiceList(BasicRadio* radio, SimpleController* controller);
void RenderSimple_Service(BasicRadio* radio, SimpleController* controller);
void RenderSimple_ServiceComponentList(BasicRadio* radio, SimpleController* controller);
void RenderSimple_ServiceComponent(BasicRadio* radio, SimpleController* controller);
void RenderSimple_LinkServices(BasicRadio* radio, SimpleController* controller);
void RenderSimple_LinkService(BasicRadio* radio, SimpleController* controller, LinkService* link_service);

void RenderBasicRadio(BasicRadio* radio) {
    auto lock_db = std::scoped_lock(radio->GetDatabaseMutex());
    auto lock_channels = std::scoped_lock(radio->GetChannelsMutex());

    RenderSubchannels(radio);
    static SimpleController simple_controller;
    RenderSimple_Root(radio, &simple_controller);
}

// Render a list of all subchannels
void RenderSubchannels(BasicRadio* radio) {
    auto db = radio->GetDatabase();
    auto window_label = fmt::format("Subchannels ({})###Subchannels Full List", db->subchannels.size());
    if (ImGui::Begin(window_label.c_str())) {
        auto& search_filter = GlobalFilters.services_filter;
        search_filter.Draw();

        ImGuiTableFlags flags = ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Hideable | ImGuiTableFlags_Borders;
        if (ImGui::BeginTable("Subchannels table", 6, flags)) 
        {
            ImGui::TableSetupColumn("Service Label",    ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("ID",               ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Start Address",    ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Capacity Units",   ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Protection",       ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Bitrate",          ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();

            int row_id  = 0;
            for (auto& subchannel: db->subchannels) {
                auto service_component = db->GetServiceComponent_Subchannel(subchannel.id);
                auto service = service_component ? db->GetService(service_component->service_reference) : NULL;
                auto service_label = service ? service->label.c_str() : "";
                if (!search_filter.PassFilter(service_label)) {
                    continue;
                }

                const auto prot_label = GetSubchannelProtectionLabel(subchannel);
                const uint32_t bitrate_kbps = GetSubchannelBitrate(subchannel);
                const bool is_selected = radio->IsSubchannelAdded(subchannel.id);

                ImGui::PushID(row_id++);

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextWrapped("%s", service_label);
                ImGui::TableSetColumnIndex(1);
                ImGui::TextWrapped("%u", subchannel.id);
                ImGui::TableSetColumnIndex(2);
                ImGui::TextWrapped("%u", subchannel.start_address);
                ImGui::TableSetColumnIndex(3);
                ImGui::TextWrapped("%u", subchannel.length);
                ImGui::TableSetColumnIndex(4);
                ImGui::TextWrapped("%.*s", prot_label.length(), prot_label.c_str());
                ImGui::TableSetColumnIndex(5);
                ImGui::TextWrapped("%u kb/s", bitrate_kbps);
                ImGui::SameLine();
                if (ImGui::Selectable("###select_button", is_selected, ImGuiSelectableFlags_SpanAllColumns)) {
                    radio->AddSubchannel(subchannel.id);
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    }
    ImGui::End();
}

// Render the ensemble information
void RenderEnsemble(BasicRadio* radio) {
    auto db = radio->GetDatabase();
    auto& ensemble = db->ensemble;

    if (ImGui::Begin("Ensemble")) {
        ImGuiTableFlags flags = ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Hideable | ImGuiTableFlags_Borders;
        if (ImGui::BeginTable("Ensemble description", 2, flags)) {
            ImGui::TableSetupColumn("Field", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();

            #define FIELD_MACRO(name, fmt, ...) {\
                ImGui::PushID(row_id++);\
                ImGui::TableNextRow();\
                ImGui::TableSetColumnIndex(0);\
                ImGui::TextWrapped(name);\
                ImGui::TableSetColumnIndex(1);\
                ImGui::TextWrapped(fmt, ##__VA_ARGS__);\
                ImGui::PopID();\
            }\

            int row_id = 0;
            const float LTO = static_cast<float>(ensemble.local_time_offset) / 10.0f;
            FIELD_MACRO("Name", "%.*s", ensemble.label.length(), ensemble.label.c_str());
            FIELD_MACRO("ID", "%u", ensemble.reference);
            FIELD_MACRO("Country ID", "%u", ensemble.country_id);
            FIELD_MACRO("Extended Country Code", "0x%02X", ensemble.extended_country_code);
            FIELD_MACRO("Local Time Offset", "%.1f hours", LTO);
            FIELD_MACRO("Inter Table ID", "%u", ensemble.international_table_id);
            FIELD_MACRO("Total Services", "%u", ensemble.nb_services);
            FIELD_MACRO("Total Reconfig", "%u", ensemble.reconfiguration_count);
            #undef FIELD_MACRO

            ImGui::EndTable();
        }
    }
    ImGui::End();
}

// Render misc information about the date and time
void RenderDateTime(BasicRadio* radio) {
    const auto info = radio->GetDABMiscInfo();
    if (ImGui::Begin("Date & Time")) {
        ImGuiTableFlags flags = ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Hideable | ImGuiTableFlags_Borders;
        if (ImGui::BeginTable("Date & Time", 2, flags)) {
            ImGui::TableSetupColumn("Field", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();

            #define FIELD_MACRO(name, fmt, ...) {\
                ImGui::PushID(row_id++);\
                ImGui::TableNextRow();\
                ImGui::TableSetColumnIndex(0);\
                ImGui::TextWrapped(name);\
                ImGui::TableSetColumnIndex(1);\
                ImGui::TextWrapped(fmt, ##__VA_ARGS__);\
                ImGui::PopID();\
            }\

            int row_id = 0;
            FIELD_MACRO("Date", "%02d/%02d/%04d", 
                info.datetime.day, info.datetime.month, info.datetime.year);
            FIELD_MACRO("Time", "%02u:%02u:%02u.%03u", 
                info.datetime.hours, info.datetime.minutes, 
                info.datetime.seconds, info.datetime.milliseconds);
            FIELD_MACRO("CIF Counter", "%+4u = %+2u|%-3u", 
                info.cif_counter.GetTotalCount(),
                info.cif_counter.upper_count, info.cif_counter.lower_count);

            #undef FIELD_MACRO
            ImGui::EndTable();
        }
    }
    ImGui::End();
}

// Database statistics
void RenderDatabaseStatistics(BasicRadio* radio) {
    const auto stats = radio->GetDatabaseStatistics();
    if (ImGui::Begin("Database Stats")) {
        ImGuiTableFlags flags = ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Hideable | ImGuiTableFlags_Borders;
        if (ImGui::BeginTable("Date & Time", 2, flags)) {
            ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Count", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();

            #define FIELD_MACRO(name, fmt, ...) {\
                ImGui::PushID(row_id++);\
                ImGui::TableNextRow();\
                ImGui::TableSetColumnIndex(0);\
                ImGui::TextWrapped(name);\
                ImGui::TableSetColumnIndex(1);\
                ImGui::TextWrapped(fmt, ##__VA_ARGS__);\
                ImGui::PopID();\
            }\

            int row_id = 0;
            FIELD_MACRO("Total", "%d", stats.nb_total);
            FIELD_MACRO("Pending", "%d", stats.nb_pending);
            FIELD_MACRO("Completed", "%d", stats.nb_completed);
            FIELD_MACRO("Conflicts", "%d", stats.nb_conflicts);
            FIELD_MACRO("Updates", "%d", stats.nb_updates);

            #undef FIELD_MACRO
            ImGui::EndTable();
        }
    }
    ImGui::End();
}

// Linked ensembles
void RenderOtherEnsembles(BasicRadio* radio) {
    auto* db = radio->GetDatabase();
    auto label = fmt::format("Other Ensembles ({})###Other Ensembles",
        db->other_ensembles.size());

    if (ImGui::Begin(label.c_str())) {
        ImGuiTableFlags flags = ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Hideable | ImGuiTableFlags_Borders;
        if (ImGui::BeginTable("Components table", 6, flags)) 
        {
            ImGui::TableSetupColumn("Reference",            ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Country ID",     ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Continuous Output",        ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Geographically Adjacent",    ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Mode I",   ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Frequency",             ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();

            int row_id  = 0;
            for (auto& ensemble: db->other_ensembles) {
                ImGui::PushID(row_id++);

                const float frequency =  static_cast<float>(ensemble.frequency) * 1e-6f;

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextWrapped("%u", ensemble.reference);
                ImGui::TableSetColumnIndex(1);
                ImGui::TextWrapped("%u", ensemble.country_id);
                ImGui::TableSetColumnIndex(2);
                ImGui::TextWrapped("%s", ensemble.is_continuous_output ? "Yes" : "No");
                ImGui::TableSetColumnIndex(3);
                ImGui::TextWrapped("%s", ensemble.is_geographically_adjacent ? "Yes" : "No");
                ImGui::TableSetColumnIndex(4);
                ImGui::TextWrapped("%s", ensemble.is_transmission_mode_I ? "Yes" : "No");
                ImGui::TableSetColumnIndex(5);
                ImGui::TextWrapped("%3.3f MHz", frequency);

                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    }
    ImGui::End();
}

// Render a list of the services
void RenderSimple_Root(BasicRadio* radio, SimpleController* controller) {
    auto db = radio->GetDatabase();
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

void RenderSimple_ServiceList(BasicRadio* radio, SimpleController* controller) {
    auto db = radio->GetDatabase();
    const auto window_title = fmt::format("Services ({})###Services panel", db->services.size());
    if (ImGui::Begin(window_title.c_str())) {
        auto& search_filter = GlobalFilters.services_filter;
        search_filter.Draw("###Services search filter", -1.0f);
        if (ImGui::BeginListBox("###Services list", ImVec2(-1,-1))) {
            for (auto& service: db->services) {
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

void RenderSimple_Service(BasicRadio* radio, SimpleController* controller) {
    auto* db = radio->GetDatabase();
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

void RenderSimple_ServiceComponentList(BasicRadio* radio, SimpleController* controller) {
    auto* db = radio->GetDatabase();
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

                const bool is_selected = radio->IsSubchannelAdded(component->subchannel_id);

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
                ImGui::SameLine();
                if (ImGui::Selectable("###select_button", is_selected, ImGuiSelectableFlags_SpanAllColumns)) {
                    radio->AddSubchannel(component->subchannel_id);
                }

                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    }
    ImGui::End();
}

void RenderSimple_ServiceComponent(BasicRadio* radio, SimpleController* controller) {

}

void RenderSimple_LinkServices(BasicRadio* radio, SimpleController* controller) {
    auto* db = radio->GetDatabase();
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

void RenderSimple_LinkService(BasicRadio* radio, SimpleController* controller, LinkService* link_service) {
    auto db = radio->GetDatabase();
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