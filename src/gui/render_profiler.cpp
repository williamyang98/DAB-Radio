#include "render_profiler.h"
#include <imgui.h>
#include "utility/profiler.h"

void RenderProfiler() {
    auto& instrumentor = Instrumentor::Get();


    if (ImGui::Begin("Profiler")) {
        static InstrumentorThread* thread = NULL;

        const ImGuiTableFlags flags = ImGuiTableFlags_BordersV | ImGuiTableFlags_BordersOuterH | ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg | ImGuiTableFlags_NoBordersInBody;
        if (ImGui::BeginTable("Threads", 2, flags)) {
            // The first column will use the default _WidthStretch when ScrollX is Off and _WidthFixed when ScrollX is On
            ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_NoHide);
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_NoHide);
            ImGui::TableHeadersRow();


            int row_id = 0;
            for (auto& [thread_id, instrumentor_thread]: instrumentor.GetThreadsList()) {
                const bool is_selected = (thread == &instrumentor_thread);

                ImGui::PushID(row_id++);
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Text("%zu", thread_id);
                ImGui::TableNextColumn();
                if (ImGui::Selectable(instrumentor_thread.GetLabel(), is_selected, ImGuiSelectableFlags_SpanAllColumns)) {
                    if (is_selected) {
                        thread = NULL;
                    } else {
                        thread = &instrumentor_thread;
                    }
                }
                ImGui::PopID();
            }

            ImGui::EndTable();
        }

        if (ImGui::BeginChild("Thread data") && (thread != NULL)) {
            auto& mutex = thread->GetMutex();
            auto lock = std::scoped_lock(mutex);
            auto& results = thread->GetResults(); 
            const int N = (int)results.size();

            static ImGuiTableFlags flags = ImGuiTableFlags_BordersV | ImGuiTableFlags_BordersOuterH | ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg | ImGuiTableFlags_NoBordersInBody;
            if (ImGui::BeginTable("Results", 2, flags)) {
                // The first column will use the default _WidthStretch when ScrollX is Off and _WidthFixed when ScrollX is On
                ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_NoHide);
                ImGui::TableSetupColumn("Duration (us)", ImGuiTableColumnFlags_NoHide);
                ImGui::TableHeadersRow();

                // Keep track of position in tree 
                int prev_stack_index = 0;
                bool show_node = true;
                for (int i = 0; i < N; i++) {
                    auto& result = results[i];
                    // dont render child nodes
                    if (!show_node && (result.stack_index > prev_stack_index)) {
                        continue;
                    }

                    const bool is_parent = (i != (N-1)) && (results[i+1].stack_index > result.stack_index);
                    const int indent = result.stack_index-prev_stack_index;
                    prev_stack_index = result.stack_index;

                    if (indent < 0) {
                        for (int i = 0; i < -indent; i++) {
                            ImGui::TreePop();
                        }
                    }

                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    if (is_parent) {
                        const bool is_open = ImGui::TreeNodeEx(result.name, ImGuiTreeNodeFlags_SpanFullWidth);
                        show_node = is_open;
                    } else {
                        ImGui::TreeNodeEx(result.name, ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_Bullet | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanFullWidth);
                    } 
                    ImGui::TableNextColumn();
                    ImGui::Text("%lld", result.end-result.start);
                }

                while (prev_stack_index > 0) {
                    ImGui::TreePop();
                    prev_stack_index--;
                }

                ImGui::EndTable();
            }
        }
        ImGui::EndChild();
    }
    ImGui::End();
}