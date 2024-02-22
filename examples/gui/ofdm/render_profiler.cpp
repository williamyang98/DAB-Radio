#include "./render_profiler.h"
#include "ofdm/profiler.h"

#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>
#include <stdint.h>
#include <inttypes.h>

static void RenderTrace(const InstrumentorThread::profile_trace_t& trace);
static void RenderLoggedTraces(const InstrumentorThread::profile_trace_logger_t& traces);

void RenderProfiler() {
    auto& instrumentor = Instrumentor::Get();

    if (ImGui::Begin("Profiler")) {
        static InstrumentorThread* thread = nullptr;
        static std::hash<std::thread::id> thread_id_hasher;

        const ImGuiTableFlags flags = ImGuiTableFlags_BordersV | ImGuiTableFlags_BordersOuterH | ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg | ImGuiTableFlags_NoBordersInBody;
        if (ImGui::BeginTable("Threads", 3, flags)) {
            // The first column will use the default _WidthStretch when ScrollX is Off and _WidthFixed when ScrollX is On
            ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_NoHide);
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_NoHide);
            ImGui::TableSetupColumn("Description", ImGuiTableColumnFlags_NoHide);
            ImGui::TableHeadersRow();

            int row_id = 0;
            auto lock = std::unique_lock(instrumentor.GetMutexThreadsList());
            for (auto& [thread_id, instrumentor_thread]: instrumentor.GetThreadsList()) {
                const bool is_selected = (thread == &instrumentor_thread);
                const size_t thread_id_hash = thread_id_hasher(thread_id);

                ImGui::PushID(row_id++);
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Text("%zu", thread_id_hash);
                ImGui::TableNextColumn();
                if (ImGui::Selectable(instrumentor_thread.GetLabel(), is_selected, ImGuiSelectableFlags_SpanAllColumns)) {
                    if (is_selected) {
                        thread = nullptr;
                    } else {
                        thread = &instrumentor_thread;
                    }
                }
                ImGui::TableNextColumn();
                const auto& data_opt = instrumentor_thread.GetData();
                if (data_opt.has_value()) {
                    const auto& data = data_opt.value();
                    const size_t total_symbols = data.symbol_end-data.symbol_start;
                    ImGui::Text("Start=%-2zu End=%-2zu Total=%-2zu", data.symbol_start, data.symbol_end, total_symbols);
                }
                ImGui::PopID();
            }

            ImGui::EndTable();
        }

        ImGuiTabBarFlags tab_bar_flags = ImGuiTabBarFlags_None;
        if (ImGui::BeginTabBar("Trace Viewer", tab_bar_flags))
        {
            if ((thread != nullptr) && ImGui::BeginTabItem("Last Trace")) {
                auto lock = std::scoped_lock(thread->GetPrevTraceMutex());
                const auto& trace = thread->GetPrevTrace(); 
                RenderTrace(trace);
                ImGui::EndTabItem();
            }
            if ((thread != nullptr) && (thread->GetIsLogTraces()) && ImGui::BeginTabItem("Trace Logs")) {
                auto lock = std::scoped_lock(thread->GetTraceLogsMutex());
                const auto& traces = thread->GetTraceLogs();
                RenderLoggedTraces(traces);
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }

    }
    ImGui::End();
}

void RenderLoggedTraces(const InstrumentorThread::profile_trace_logger_t& traces) {
    const InstrumentorThread::profile_trace_t* selected_trace = nullptr;

    const ImGuiTableFlags flags = ImGuiTableFlags_BordersV | ImGuiTableFlags_BordersOuterH | ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg | ImGuiTableFlags_NoBordersInBody;
    if (ImGui::BeginTable("Traces", 3, flags)) {
        static uint64_t selected_key = 0;
        // The first column will use the default _WidthStretch when ScrollX is Off and _WidthFixed when ScrollX is On
        ImGui::TableSetupColumn("Length", ImGuiTableColumnFlags_NoHide);
        ImGui::TableSetupColumn("Hash", ImGuiTableColumnFlags_NoHide);
        ImGui::TableSetupColumn("Count", ImGuiTableColumnFlags_NoHide);
        ImGui::TableHeadersRow();

        int row_id = 0;
        for (auto& [key, log]: traces) {
            const bool is_selected = (key == selected_key);
            const int count = log.count;
            auto& trace = log.trace;
            if (is_selected) {
                selected_trace = &trace;
            }

            ImGui::PushID(row_id++);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("%zu", trace.size());
            ImGui::TableNextColumn();
            static char hash_string[32];
            snprintf(hash_string, sizeof(hash_string), "%" PRIx64, key);
            if (ImGui::Selectable(hash_string, is_selected, ImGuiSelectableFlags_SpanAllColumns)) {
                if (is_selected) {
                    selected_key = 0;
                } else {
                    selected_key = key;
                }
            }
            ImGui::TableNextColumn();
            ImGui::Text("%d", count);
            ImGui::PopID();
        }

        ImGui::EndTable();
    }

    if (selected_trace != nullptr) {
        RenderTrace(*selected_trace);
    }
}

void RenderTrace(const InstrumentorThread::profile_trace_t& trace) {
    const int N = (int)trace.size();
    static ImGuiTableFlags flags = ImGuiTableFlags_BordersV | ImGuiTableFlags_BordersOuterH | ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg | ImGuiTableFlags_NoBordersInBody;
    if (ImGui::BeginTable("Results", 4, flags)) {
        // The first column will use the default _WidthStretch when ScrollX is Off and _WidthFixed when ScrollX is On
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_NoHide);
        ImGui::TableSetupColumn("Duration (us)", ImGuiTableColumnFlags_NoHide);
        ImGui::TableSetupColumn("Start (us)", ImGuiTableColumnFlags_NoHide);
        ImGui::TableSetupColumn("End (us)", ImGuiTableColumnFlags_NoHide);
        ImGui::TableHeadersRow();

        // Keep track of position in tree 
        int prev_stack_index = 0;
        bool show_node = true;
        for (int i = 0; i < N; i++) {
            const auto& result = trace[i];
            // dont render child nodes
            if (!show_node && (result.stack_index > prev_stack_index)) {
                continue;
            }

            const bool is_parent = (i != (N-1)) && (trace[i+1].stack_index > result.stack_index);
            const int indent = result.stack_index-prev_stack_index;
            prev_stack_index = result.stack_index;

            if (indent < 0) {
                for (int j = 0; j < -indent; j++) {
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
            ImGui::Text("%" PRIi64, result.end-result.start);
            ImGui::TableNextColumn();
            ImGui::Text("%" PRIi64, result.start);
            ImGui::TableNextColumn();
            ImGui::Text("%" PRIi64, result.end);
        }

        while (prev_stack_index > 0) {
            ImGui::TreePop();
            prev_stack_index--;
        }

        ImGui::EndTable();
    }
}
