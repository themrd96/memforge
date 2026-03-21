#include "gui/app.h"
#include "core/undo_history.h"
#include <imgui.h>
#include <cstring>
#include <sstream>
#include <iomanip>

namespace memforge {

void DrawUndoHistory(App& app) {
    ImGui::SetNextWindowSize(ImVec2(750, 400), ImGuiCond_FirstUseEver);

    if (!ImGui::Begin("Undo History", &app.showUndoHistory)) {
        ImGui::End();
        return;
    }

    bool attached = app.processAttached && app.targetProcess;
    const auto& history = app.undoHistory.GetHistory();

    // ─── Toolbar ─────────────────────────────────────────────

    ImGui::BeginDisabled(!attached || history.empty());
    if (ImGui::Button("Undo Last", ImVec2(100, 28))) {
        app.undoHistory.Undo(app.targetProcess);
    }
    ImGui::SameLine();
    if (ImGui::Button("Undo All", ImVec2(100, 28))) {
        for (const auto& entry : history) {
            if (!entry.undone) {
                app.undoHistory.UndoById(app.targetProcess, entry.id);
            }
        }
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::BeginDisabled(history.empty());
    if (ImGui::Button("Clear History", ImVec2(120, 28))) {
        app.undoHistory.Clear();
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::TextDisabled("(%zu entries)", history.size());

    ImGui::Separator();
    ImGui::Spacing();

    // ─── History Table ───────────────────────────────────────

    if (history.empty()) {
        ImGui::TextDisabled("No write history. Writes will be recorded here automatically.");
        ImGui::End();
        return;
    }

    if (ImGui::BeginTable("##undohistory", 7,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                          ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY,
                          ImVec2(0, 0))) {
        ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 40.0f);
        ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("Address", ImGuiTableColumnFlags_WidthFixed, 120.0f);
        ImGui::TableSetupColumn("Old Value", ImGuiTableColumnFlags_WidthFixed, 100.0f);
        ImGui::TableSetupColumn("New Value", ImGuiTableColumnFlags_WidthFixed, 100.0f);
        ImGui::TableSetupColumn("Description", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();

        // Show newest first
        for (int i = static_cast<int>(history.size()) - 1; i >= 0; --i) {
            const auto& entry = history[i];
            ImGui::TableNextRow();

            // Row color based on status
            if (entry.undone) {
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0,
                                       ImGui::GetColorU32(ImVec4(0.4f, 0.4f, 0.1f, 0.15f)));
            }

            // ID
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%d", entry.id);

            // Time (relative, seconds ago)
            ImGui::TableSetColumnIndex(1);
            {
                uint64_t now = GetTickCount64();
                uint64_t elapsed = (now > entry.timestamp) ? (now - entry.timestamp) / 1000 : 0;
                if (elapsed < 60) {
                    ImGui::Text("%llus ago", static_cast<unsigned long long>(elapsed));
                } else if (elapsed < 3600) {
                    ImGui::Text("%llum ago", static_cast<unsigned long long>(elapsed / 60));
                } else {
                    ImGui::Text("%lluh ago", static_cast<unsigned long long>(elapsed / 3600));
                }
            }

            // Address
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("0x%llX", static_cast<unsigned long long>(entry.address));

            // Old value (hex)
            ImGui::TableSetColumnIndex(3);
            {
                std::ostringstream oss;
                for (size_t b = 0; b < (std::min)(entry.oldValue.size(), static_cast<size_t>(8)); ++b) {
                    if (b > 0) oss << ' ';
                    oss << std::hex << std::uppercase << std::setw(2) << std::setfill('0')
                        << static_cast<int>(entry.oldValue[b]);
                }
                if (entry.oldValue.size() > 8) oss << "...";
                ImGui::TextDisabled("%s", oss.str().c_str());
            }

            // New value (hex)
            ImGui::TableSetColumnIndex(4);
            {
                std::ostringstream oss;
                for (size_t b = 0; b < (std::min)(entry.newValue.size(), static_cast<size_t>(8)); ++b) {
                    if (b > 0) oss << ' ';
                    oss << std::hex << std::uppercase << std::setw(2) << std::setfill('0')
                        << static_cast<int>(entry.newValue[b]);
                }
                if (entry.newValue.size() > 8) oss << "...";
                ImGui::Text("%s", oss.str().c_str());
            }

            // Description
            ImGui::TableSetColumnIndex(5);
            if (entry.undone) {
                ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.2f, 1.0f), "%s [undone]",
                                  entry.description.c_str());
            } else {
                ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.3f, 1.0f), "%s",
                                  entry.description.c_str());
            }

            // Actions
            ImGui::TableSetColumnIndex(6);
            ImGui::BeginDisabled(!attached);
            if (entry.undone) {
                std::string redoLabel = "Redo##" + std::to_string(entry.id);
                if (ImGui::SmallButton(redoLabel.c_str())) {
                    app.undoHistory.Redo(app.targetProcess, entry.id);
                }
            } else {
                std::string undoLabel = "Undo##" + std::to_string(entry.id);
                if (ImGui::SmallButton(undoLabel.c_str())) {
                    app.undoHistory.UndoById(app.targetProcess, entry.id);
                }
            }
            ImGui::EndDisabled();
        }

        ImGui::EndTable();
    }

    ImGui::End();
}

} // namespace memforge
