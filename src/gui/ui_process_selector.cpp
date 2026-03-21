#include "gui/app.h"
#include <imgui.h>
#include <algorithm>
#include <cstring>

namespace memforge {

void DrawProcessSelector(App& app) {
    ImGui::SetNextWindowSize(ImVec2(450, 500), ImGuiCond_FirstUseEver);

    if (!ImGui::Begin("Process Selector", &app.showProcessSelector)) {
        ImGui::End();
        return;
    }

    // Toolbar
    if (ImGui::Button("Refresh")) {
        app.RefreshProcessList();
    }
    ImGui::SameLine();
    if (app.processAttached) {
        if (ImGui::Button("Detach")) {
            app.DetachFromProcess();
        }
    }

    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##filter", "Filter processes...",
                             app.processFilter, sizeof(app.processFilter));

    ImGui::Separator();

    // Process list table
    if (ImGui::BeginTable("ProcessTable", 4,
                          ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg |
                          ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Sortable |
                          ImGuiTableFlags_Resizable,
                          ImVec2(0, -1))) {

        ImGui::TableSetupScrollFreeze(0, 1); // freeze header row
        ImGui::TableSetupColumn("PID", ImGuiTableColumnFlags_WidthFixed, 60.0f);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Window", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Memory", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableHeadersRow();

        std::string filterStr = app.processFilter;
        std::transform(filterStr.begin(), filterStr.end(), filterStr.begin(), ::tolower);

        for (auto& proc : app.processList) {
            // Skip system idle process
            if (proc.pid == 0) continue;

            // Apply filter
            if (!filterStr.empty()) {
                std::string nameLower = proc.name;
                std::transform(nameLower.begin(), nameLower.end(),
                             nameLower.begin(), ::tolower);
                std::string titleLower = proc.windowTitle;
                std::transform(titleLower.begin(), titleLower.end(),
                             titleLower.begin(), ::tolower);

                if (nameLower.find(filterStr) == std::string::npos &&
                    titleLower.find(filterStr) == std::string::npos) {
                    continue;
                }
            }

            ImGui::TableNextRow();

            // Highlight attached process
            bool isAttached = (proc.pid == app.targetPid && app.processAttached);
            if (isAttached) {
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0,
                                       ImGui::GetColorU32(ImVec4(0.15f, 0.30f, 0.15f, 0.5f)));
            }

            ImGui::TableNextColumn();
            char pidStr[32];
            snprintf(pidStr, sizeof(pidStr), "%lu", proc.pid);

            bool selected = false;
            if (ImGui::Selectable(pidStr, isAttached,
                                  ImGuiSelectableFlags_SpanAllColumns |
                                  ImGuiSelectableFlags_AllowDoubleClick)) {
                if (ImGui::IsMouseDoubleClicked(0)) {
                    app.AttachToProcess(proc.pid);
                }
            }

            // Right-click context menu
            if (ImGui::BeginPopupContextItem()) {
                if (ImGui::MenuItem("Attach")) {
                    app.AttachToProcess(proc.pid);
                }
                if (ImGui::MenuItem("Copy PID")) {
                    ImGui::SetClipboardText(pidStr);
                }
                ImGui::EndPopup();
            }

            ImGui::TableNextColumn();
            ImGui::TextUnformatted(proc.name.c_str());

            ImGui::TableNextColumn();
            if (!proc.windowTitle.empty()) {
                ImGui::TextUnformatted(proc.windowTitle.c_str());
            } else {
                ImGui::TextDisabled("(no window)");
            }

            ImGui::TableNextColumn();
            if (proc.memoryUsage > 0) {
                float mb = proc.memoryUsage / (1024.0f * 1024.0f);
                ImGui::Text("%.1f MB", mb);
            }
        }

        ImGui::EndTable();
    }

    ImGui::End();
}

} // namespace memforge
