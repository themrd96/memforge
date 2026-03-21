#include "gui/app.h"
#include "core/memory_snapshot.h"
#include <imgui.h>
#include <cstring>
#include <sstream>
#include <iomanip>

namespace memforge {

static int s_filterMode = 0; // 0 = All, 1 = Increased, 2 = Decreased
static std::vector<MemoryDiff> s_filteredDiffs;
static bool s_needsFilter = false;

void DrawSnapshots(App& app) {
    ImGui::SetNextWindowSize(ImVec2(700, 500), ImGuiCond_FirstUseEver);

    if (!ImGui::Begin("Memory Snapshots", &app.showSnapshots)) {
        ImGui::End();
        return;
    }

    bool attached = app.processAttached && app.targetProcess;
    if (!attached) {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.3f, 1.0f), "Attach to a process first.");
        ImGui::End();
        return;
    }

    // ─── Snapshot Controls ───────────────────────────────────

    if (ImGui::Button("Take Snapshot A", ImVec2(160, 30))) {
        app.snapshotA.Capture(app.targetProcess);
        app.snapshotDiffs.clear();
        s_filteredDiffs.clear();
    }
    ImGui::SameLine();
    if (app.snapshotA.IsValid()) {
        size_t sizeMB = app.snapshotA.GetTotalSize() / (1024 * 1024);
        ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.3f, 1.0f),
                          "A: %zu MB (%zu regions)", sizeMB, app.snapshotA.GetRegionCount());
    } else {
        ImGui::TextDisabled("A: not captured");
    }

    if (ImGui::Button("Take Snapshot B", ImVec2(160, 30))) {
        app.snapshotB.Capture(app.targetProcess);
        app.snapshotDiffs.clear();
        s_filteredDiffs.clear();
    }
    ImGui::SameLine();
    if (app.snapshotB.IsValid()) {
        size_t sizeMB = app.snapshotB.GetTotalSize() / (1024 * 1024);
        ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.3f, 1.0f),
                          "B: %zu MB (%zu regions)", sizeMB, app.snapshotB.GetRegionCount());
    } else {
        ImGui::TextDisabled("B: not captured");
    }

    ImGui::Spacing();

    bool canCompare = app.snapshotA.IsValid() && app.snapshotB.IsValid();
    ImGui::BeginDisabled(!canCompare);
    if (ImGui::Button("Compare", ImVec2(120, 30))) {
        app.snapshotDiffs = app.snapshotA.Compare(app.snapshotB);
        s_filterMode = 0;
        s_filteredDiffs.clear();
        s_needsFilter = true;
    }
    ImGui::EndDisabled();

    ImGui::SameLine();

    const char* filterItems[] = { "All Changes", "Increased", "Decreased" };
    ImGui::SetNextItemWidth(150);
    if (ImGui::Combo("Filter##snapfilter", &s_filterMode, filterItems, 3)) {
        s_needsFilter = true;
    }

    // Apply filter
    if (s_needsFilter && !app.snapshotDiffs.empty()) {
        switch (s_filterMode) {
            case 1:
                s_filteredDiffs = MemorySnapshot::FilterIncreased(app.snapshotDiffs);
                break;
            case 2:
                s_filteredDiffs = MemorySnapshot::FilterDecreased(app.snapshotDiffs);
                break;
            default:
                s_filteredDiffs = app.snapshotDiffs;
                break;
        }
        s_needsFilter = false;
    }

    ImGui::Separator();
    ImGui::Spacing();

    // ─── Results Table ───────────────────────────────────────

    const auto& displayDiffs = s_filteredDiffs.empty() && s_filterMode == 0 ?
                               app.snapshotDiffs : s_filteredDiffs;

    if (displayDiffs.empty()) {
        ImGui::TextDisabled("No differences to display. Take two snapshots and compare.");
        ImGui::End();
        return;
    }

    size_t displayCount = (std::min)(displayDiffs.size(), static_cast<size_t>(5000));
    if (displayDiffs.size() > 5000) {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f),
                          "Showing 5000 of %zu differences", displayDiffs.size());
    } else {
        ImGui::Text("%zu differences", displayDiffs.size());
    }

    ImGui::Spacing();

    if (ImGui::BeginTable("##snapshotresults", 5,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                          ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY,
                          ImVec2(0, 0))) {
        ImGui::TableSetupColumn("Address", ImGuiTableColumnFlags_WidthFixed, 130.0f);
        ImGui::TableSetupColumn("Before", ImGuiTableColumnFlags_WidthFixed, 130.0f);
        ImGui::TableSetupColumn("After", ImGuiTableColumnFlags_WidthFixed, 130.0f);
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("Difference", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();

        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(displayCount));
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                const auto& d = displayDiffs[row];
                ImGui::TableNextRow();

                // Address
                ImGui::TableSetColumnIndex(0);
                char addrBuf[32];
                snprintf(addrBuf, sizeof(addrBuf), "0x%llX", static_cast<unsigned long long>(d.address));
                ImGui::Selectable(addrBuf, false, ImGuiSelectableFlags_SpanAllColumns);

                // Right-click context menu
                if (ImGui::BeginPopupContextItem(("##snapctx" + std::to_string(row)).c_str())) {
                    if (ImGui::MenuItem("Copy Address")) {
                        ImGui::SetClipboardText(addrBuf);
                    }
                    if (ImGui::MenuItem("Copy Before Value")) {
                        ImGui::SetClipboardText(d.beforeStr.c_str());
                    }
                    if (ImGui::MenuItem("Copy After Value")) {
                        ImGui::SetClipboardText(d.afterStr.c_str());
                    }
                    ImGui::EndPopup();
                }

                // Before
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%s", d.beforeStr.c_str());

                // After
                ImGui::TableSetColumnIndex(2);
                ImGui::Text("%s", d.afterStr.c_str());

                // Type
                ImGui::TableSetColumnIndex(3);
                ImGui::TextDisabled("%s", d.guessedType.c_str());

                // Difference
                ImGui::TableSetColumnIndex(4);
                if (d.before.size() >= 4 && d.after.size() >= 4) {
                    int32_t bv, av;
                    std::memcpy(&bv, d.before.data(), 4);
                    std::memcpy(&av, d.after.data(), 4);
                    int32_t diff = av - bv;
                    if (diff > 0) {
                        ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.3f, 1.0f), "+%d", diff);
                    } else {
                        ImGui::TextColored(ImVec4(0.9f, 0.3f, 0.3f, 1.0f), "%d", diff);
                    }
                }
            }
        }

        ImGui::EndTable();
    }

    ImGui::End();
}

} // namespace memforge
