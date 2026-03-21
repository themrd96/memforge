#include "gui/app.h"
#include <imgui.h>
#include <cstdio>
#include <sstream>

namespace memforge {

static const char* valueTypeNames[] = {
    "Byte (1)", "2 Bytes", "4 Bytes", "8 Bytes", "Float", "Double", "Byte Array", "String"
};

static const char* scanModeNames[] = {
    "Exact Value", "Greater Than", "Less Than", "Between",
    "Unknown Initial", "Increased", "Decreased",
    "Changed", "Unchanged", "Increased By", "Decreased By"
};

void DrawScanner(App& app) {
    ImGui::SetNextWindowSize(ImVec2(700, 600), ImGuiCond_FirstUseEver);

    if (!ImGui::Begin("Memory Scanner", &app.showScanner)) {
        ImGui::End();
        return;
    }

    if (!app.processAttached) {
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f),
                          "No process attached. Double-click a process to attach.");
        ImGui::End();
        return;
    }

    // ─── Scan controls ──────────────────────────────────

    // Value type selector
    ImGui::SetNextItemWidth(150);
    ImGui::Combo("Value Type", &app.selectedValueType,
                 valueTypeNames, IM_ARRAYSIZE(valueTypeNames));

    ImGui::SameLine();

    // Scan mode selector
    ImGui::SetNextItemWidth(150);
    int maxMode = app.firstScanDone ? IM_ARRAYSIZE(scanModeNames) : 5;
    ImGui::Combo("Scan Mode", &app.selectedScanMode, scanModeNames, maxMode);

    // Value input
    ScanMode currentMode = static_cast<ScanMode>(app.selectedScanMode);
    bool needsValue = (currentMode != ScanMode::UnknownInitial &&
                       currentMode != ScanMode::Increased &&
                       currentMode != ScanMode::Decreased &&
                       currentMode != ScanMode::Changed &&
                       currentMode != ScanMode::Unchanged);

    if (needsValue) {
        ImGui::SetNextItemWidth(200);
        ImGui::InputText("Value", app.scanValueInput, sizeof(app.scanValueInput));

        if (currentMode == ScanMode::Between) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(200);
            ImGui::InputText("to", app.scanValueInput2, sizeof(app.scanValueInput2));
        }
    }

    // Scan buttons
    ImGui::Spacing();

    if (!app.firstScanDone) {
        bool canScan = !app.scanInProgress;
        if (!canScan) ImGui::BeginDisabled();
        if (ImGui::Button("First Scan", ImVec2(120, 30))) {
            app.StartScan();
        }
        if (!canScan) ImGui::EndDisabled();
    } else {
        bool canScan = !app.scanInProgress;
        if (!canScan) ImGui::BeginDisabled();
        if (ImGui::Button("Next Scan", ImVec2(120, 30))) {
            app.NextScan();
        }
        if (!canScan) ImGui::EndDisabled();

        ImGui::SameLine();
        if (ImGui::Button("New Scan", ImVec2(120, 30))) {
            app.ResetScan();
        }
    }

    if (app.scanInProgress) {
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            app.scanner.CancelScan();
        }
        ImGui::SameLine();
        ImGui::ProgressBar(app.scanProgress, ImVec2(200, 0));
    }

    ImGui::SameLine();
    ImGui::Text("Results: %zu", app.scanner.GetResultCount());

    ImGui::Separator();

    // ─── Split: Results (left) | Frozen values (right) ──

    float panelWidth = ImGui::GetContentRegionAvail().x;
    float frozenWidth = 350.0f;
    float resultsWidth = panelWidth - frozenWidth - 10.0f;

    // ─── Results table (left) ────────────────────────────

    ImGui::BeginChild("ResultsPanel", ImVec2(resultsWidth, 0), ImGuiChildFlags_Borders);
    ImGui::Text("Scan Results");
    ImGui::Separator();

    auto& results = app.scanner.GetResults();
    size_t displayCount = std::min(results.size(), (size_t)5000);

    if (displayCount < results.size()) {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f),
                          "Showing first 5000 of %zu results. Narrow your search.",
                          results.size());
    }

    if (ImGui::BeginTable("ResultsTable", 4,
                          ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg |
                          ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable,
                          ImVec2(0, -1))) {

        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Address", ImGuiTableColumnFlags_WidthFixed, 130.0f);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, 100.0f);
        ImGui::TableSetupColumn("Previous", ImGuiTableColumnFlags_WidthFixed, 100.0f);
        ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 60.0f);
        ImGui::TableHeadersRow();

        ValueType vt = static_cast<ValueType>(app.selectedValueType);
        ImGuiListClipper clipper;
        clipper.Begin((int)displayCount);

        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; row++) {
                auto& r = results[row];
                ImGui::TableNextRow();

                // Address
                ImGui::TableNextColumn();
                char addrStr[32];
                snprintf(addrStr, sizeof(addrStr), "0x%llX",
                        (unsigned long long)r.address);
                ImGui::TextUnformatted(addrStr);

                // Current value (re-read live)
                ImGui::TableNextColumn();
                ScanValue liveVal = app.scanner.ReadValue(r.address, vt);
                std::string valStr = MemoryScanner::ValueToString(liveVal, vt);
                ImGui::TextUnformatted(valStr.c_str());

                // Previous value
                ImGui::TableNextColumn();
                std::string prevStr = MemoryScanner::ValueToString(r.previousValue, vt);
                ImGui::TextDisabled("%s", prevStr.c_str());

                // Add to frozen list button
                ImGui::TableNextColumn();
                char btnId[64];
                snprintf(btnId, sizeof(btnId), "Add##%d", row);
                if (ImGui::SmallButton(btnId)) {
                    app.freezer.AddEntry(r.address, liveVal, vt,
                                        addrStr);
                }

                // Right-click menu
                char popupId[64];
                snprintf(popupId, sizeof(popupId), "ctx_%d", row);
                if (ImGui::BeginPopupContextItem(popupId)) {
                    if (ImGui::MenuItem("Copy Address")) {
                        ImGui::SetClipboardText(addrStr);
                    }
                    if (ImGui::MenuItem("View in Hex Viewer")) {
                        app.hexViewAddress = r.address;
                        snprintf(app.hexAddrInput, sizeof(app.hexAddrInput),
                                "%llX", (unsigned long long)r.address);
                        app.showHexViewer = true;
                    }
                    if (ImGui::MenuItem("Add to Frozen List")) {
                        app.freezer.AddEntry(r.address, liveVal, vt, addrStr);
                    }
                    ImGui::EndPopup();
                }
            }
        }

        ImGui::EndTable();
    }
    ImGui::EndChild();

    ImGui::SameLine();

    // ─── Frozen values panel (right) ─────────────────────

    ImGui::BeginChild("FrozenPanel", ImVec2(frozenWidth, 0), ImGuiChildFlags_Borders);
    ImGui::Text("Frozen Values");
    ImGui::Separator();

    auto& entries = app.freezer.GetEntries();

    for (int i = 0; i < (int)entries.size(); i++) {
        auto& e = entries[i];
        ImGui::PushID(e.id);

        // Active checkbox
        bool active = e.active;
        if (ImGui::Checkbox("##active", &active)) {
            app.freezer.ToggleEntry(e.id);
        }

        ImGui::SameLine();

        // Description / address
        char label[128];
        snprintf(label, sizeof(label), "%s",
                e.description.empty() ? "Address" : e.description.c_str());
        ImGui::Text("%s", label);

        // Editable value
        ImGui::SameLine();
        char valBuf[64];
        std::string valStr = MemoryScanner::ValueToString(e.value, e.type);
        strncpy(valBuf, valStr.c_str(), sizeof(valBuf) - 1);
        valBuf[sizeof(valBuf) - 1] = '\0';

        ImGui::SetNextItemWidth(80);
        char inputId[32];
        snprintf(inputId, sizeof(inputId), "##val%d", e.id);
        if (ImGui::InputText(inputId, valBuf, sizeof(valBuf),
                            ImGuiInputTextFlags_EnterReturnsTrue)) {
            ScanValue newVal = MemoryScanner::StringToValue(valBuf, e.type);
            app.freezer.UpdateEntryValue(e.id, newVal);
            // Also write immediately
            app.writer.WriteValue(e.address, newVal, e.type);
        }

        // Remove button
        ImGui::SameLine();
        if (ImGui::SmallButton("X")) {
            app.freezer.RemoveEntry(e.id);
            i--; // adjust index after removal
        }

        ImGui::PopID();
    }

    if (entries.empty()) {
        ImGui::TextDisabled("No frozen values.\nClick 'Add' on a scan result.");
    }

    ImGui::EndChild();

    ImGui::End();
}

} // namespace memforge
