#include "render_basic_radio.h"
#include "basic_radio.h"

#include "imgui.h"

void RenderBasicRadio(BasicRadio* radio) {
    auto lock_db = std::scoped_lock(radio->GetDatabaseMutex());
    auto lock_channels = std::scoped_lock(radio->GetChannelsMutex());
    auto db = radio->GetDatabase();

    static char text_buffer[256];
    static char service_name[25] = {0};

    if (ImGui::Begin("Subchannels")) {
        if (ImGui::BeginListBox("Subchannels list", ImVec2(ImGui::GetWindowWidth(), ImGui::GetWindowHeight()))) {
            for (auto& subchannel: db->subchannels) {
                auto service_component = db->GetServiceComponent_Subchannel(subchannel.id);
                auto service = service_component ? db->GetService(service_component->service_reference) : NULL;

                snprintf(text_buffer, 256, "%d[%d+%d] uep=%u label=%.*s", 
                    subchannel.id,
                    subchannel.start_address,
                    subchannel.length,
                    subchannel.is_uep,
                    service ? static_cast<int>(service->label.length()) : 0,
                    service ? service->label.c_str() : NULL);

                const bool is_selected = radio->IsSubchannelAdded(subchannel.id);
                if (ImGui::Selectable(text_buffer, is_selected)) {
                    radio->AddSubchannel(subchannel.id);
                }
            }
            ImGui::EndListBox();
        }
    }
    ImGui::End();
}