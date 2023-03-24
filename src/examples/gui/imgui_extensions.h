#pragma once
#include <imgui.h>

namespace ImGui {
    void BeginGroupPanel(const char* name, const ImVec2& size = ImVec2(0.0f, 0.0f));
    void EndGroupPanel();
};
