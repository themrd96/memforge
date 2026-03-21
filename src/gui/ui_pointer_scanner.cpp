#include "gui/app.h"
#include "core/pointer_scanner.h"
#include <imgui.h>
#include <sstream>

namespace memforge {

void DrawPointerScanner(App& app) {
    ImGui::SetNextWindowSize(ImVec2(700, 450), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Pointer Scanner", &app.showPointerScanner)) {
        ImGui::End();
        return;
    }

    // Target address
    ImGui::Text("Target Address:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(200);
    ImGui::InputText("##PtrTarget", app.ptrScanAddrInput, sizeof(app.ptrScanAddrInput),
                     ImGuiInputTextFlags_CharsHexadecimal);

    // Settings
    ImGui::SliderInt("Max Level", &app.ptrScanMaxLevel, 1, 8);
    ImGui::SliderInt("Max Offset", &app.ptrScanMaxOffset, 256, 8192);

    ImGui::Separator();

    // Buttons
    bool scanning = app.pointerScanner.IsScanning();

    if (!scanning) {
        if (ImGui::Button("Start Scan") && app.processAttached) {
            uintptr_t addr = 0;
            std::istringstream iss(app.ptrScanAddrInput);
            iss >> std::hex >> addr;
            if (addr != 0) {
                PointerScanConfig config;
                config.targetAddress = addr;
                config.maxLevel = app.ptrScanMaxLevel;
                config.maxOffset = app.ptrScanMaxOffset;
                app.pointerScanner.StartScan(app.targetProcess, app.targetPid, config,
                    [&app](float progress, size_t count) {
                        app.ptrScanProgress = progress;
                        app.ptrScanResultCount = count;
                    });
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Rescan") && app.processAttached) {
            uintptr_t addr = 0;
            std::istringstream iss(app.ptrScanAddrInput);
            iss >> std::hex >> addr;
            if (addr != 0) {
                app.pointerScanner.Rescan(app.targetProcess, addr);
            }
        }
    } else {
        if (ImGui::Button("Cancel")) {
            app.pointerScanner.CancelScan();
        }
        ImGui::SameLine();
        ImGui::ProgressBar(app.ptrScanProgress);
    }

    ImGui::Separator();

    // Results table
    auto& results = app.pointerScanner.GetResults();
    ImGui::Text("Results: %zu", results.size());

    if (ImGui::BeginTable("PtrResults", 4,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                          ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY,
                          ImVec2(0, 0))) {
        ImGui::TableSetupColumn("Module", ImGuiTableColumnFlags_WidthFixed, 120);
        ImGui::TableSetupColumn("Base Offset", ImGuiTableColumnFlags_WidthFixed, 120);
        ImGui::TableSetupColumn("Pointer Path", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Resolved", ImGuiTableColumnFlags_WidthFixed, 140);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();

        // Show max 1000 results for performance
        size_t maxShow = (std::min)(results.size(), (size_t)1000);
        for (size_t i = 0; i < maxShow; i++) {
            auto& path = results[i];
            ImGui::TableNextRow();

            // Module
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(path.moduleName.c_str());

            // Base Offset
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("0x%llX", static_cast<unsigned long long>(path.baseAddress));

            // Path
            ImGui::TableSetColumnIndex(2);
            std::ostringstream pathStr;
            for (size_t j = 0; j < path.offsets.size(); j++) {
                if (j > 0) pathStr << " -> ";
                pathStr << "0x" << std::hex << path.offsets[j];
            }
            ImGui::TextUnformatted(pathStr.str().c_str());

            // Resolved
            ImGui::TableSetColumnIndex(3);
            if (app.processAttached) {
                uintptr_t resolved = path.Resolve(app.targetProcess);
                ImGui::Text("0x%llX", static_cast<unsigned long long>(resolved));
            } else {
                ImGui::TextDisabled("N/A");
            }
        }

        ImGui::EndTable();
    }

    ImGui::End();
}

} // namespace memforge
