#include "./render_common.h"

#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>
#include <fmt/core.h>
#include "basic_radio/basic_radio.h"
#include "dab/database/dab_database.h"
#include "dab/database/dab_database_updater.h"
#include "./formatters.h"

template <typename T, typename F>
static T* find_by_callback(std::vector<T>& vec, F&& func) {
    for (auto& e: vec) {
        if (func(e)) return &e;
    }
    return nullptr;
}

// Render a list of all subchannels
void RenderSubchannels(BasicRadio& radio) {
    auto& db = radio.GetDatabase();
    auto window_label = fmt::format("Subchannels ({})###Subchannels Full List", db.subchannels.size());
    if (ImGui::Begin(window_label.c_str())) {
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
            for (auto& subchannel: db.subchannels) {
                auto* service_component = find_by_callback(
                    db.service_components, 
                    [&subchannel](const auto& e) { 
                        return e.subchannel_id == subchannel.id; 
                    }
                );
                Service* service = nullptr;
                if (service_component) {
                    service = find_by_callback(
                        db.services,
                        [&service_component](const auto& e) {
                            return e.reference == service_component->service_reference;
                        }
                    );
                }
                auto service_label = service ? service->label.c_str() : "";

                const auto prot_label = GetSubchannelProtectionLabel(subchannel);
                const uint32_t bitrate_kbps = GetSubchannelBitrate(subchannel);

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
                ImGui::TextWrapped("%.*s", int(prot_label.length()), prot_label.c_str());
                ImGui::TableSetColumnIndex(5);
                ImGui::TextWrapped("%u kb/s", bitrate_kbps);

                auto* dab_plus_channel = radio.Get_DAB_Plus_Channel(subchannel.id);
                if (dab_plus_channel != NULL) {
                    auto& controls = dab_plus_channel->GetControls();
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

// Render the ensemble information
void RenderEnsemble(BasicRadio& radio) {
    auto& db = radio.GetDatabase();
    auto& ensemble = db.ensemble;

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
                ImGui::TextWrapped(fmt, __VA_ARGS__);\
                ImGui::PopID();\
            }\

            int row_id = 0;
            const float LTO = float(ensemble.local_time_offset) / 10.0f;
            FIELD_MACRO("Name", "%.*s", int(ensemble.label.length()), ensemble.label.c_str());
            FIELD_MACRO("ID", "%u", ensemble.reference);
            FIELD_MACRO("Country Code", "%s (0x%02X.%01X)", 
                GetCountryString(ensemble.extended_country_code, ensemble.country_id),
                ensemble.extended_country_code, ensemble.country_id);
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
void RenderDateTime(BasicRadio& radio) {
    const auto& info = radio.GetMiscInfo();
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
                ImGui::TextWrapped(fmt, __VA_ARGS__);\
                ImGui::PopID();\
            }\

            int row_id = 0;
            FIELD_MACRO("Date", "%02d/%02d/%04d", 
                info.datetime.day, info.datetime.month, info.datetime.year);
            FIELD_MACRO("Time", "%02u:%02u:%02u.%03u", 
                info.datetime.hours, info.datetime.minutes, 
                info.datetime.seconds, info.datetime.milliseconds);
            FIELD_MACRO("CIF Counter", "%4u = %2u|%-3u", 
                info.cif_counter.GetTotalCount(),
                info.cif_counter.upper_count, info.cif_counter.lower_count);

            #undef FIELD_MACRO
            ImGui::EndTable();
        }
    }
    ImGui::End();
}

// Database statistics
void RenderDatabaseStatistics(BasicRadio& radio) {
    const auto& stats = radio.GetDatabaseStatistics();
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
                ImGui::TextWrapped(fmt, __VA_ARGS__);\
                ImGui::PopID();\
            }\

            int row_id = 0;
            FIELD_MACRO("Total", "%zu", stats.nb_total);
            FIELD_MACRO("Pending", "%zu", stats.nb_pending);
            FIELD_MACRO("Completed", "%zu", stats.nb_completed);
            FIELD_MACRO("Conflicts", "%zu", stats.nb_conflicts);
            FIELD_MACRO("Updates", "%zu", stats.nb_updates);

            #undef FIELD_MACRO
            ImGui::EndTable();
        }
    }
    ImGui::End();
}

// Linked ensembles
void RenderOtherEnsembles(BasicRadio& radio) {
    auto& db = radio.GetDatabase();
    auto label = fmt::format("Other Ensembles ({})###Other Ensembles",
        db.other_ensembles.size());

    if (ImGui::Begin(label.c_str())) {
        ImGuiTableFlags flags = ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Hideable | ImGuiTableFlags_Borders;
        if (ImGui::BeginTable("Components table", 6, flags)) 
        {
            ImGui::TableSetupColumn("Reference",                ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Country ID",               ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Continuous Output",        ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Geographically Adjacent",  ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Mode I",                   ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Frequency",                ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();

            int row_id  = 0;
            for (auto& ensemble: db.other_ensembles) {
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

