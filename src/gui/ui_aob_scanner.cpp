#include "gui/app.h"
#include "core/aob_scanner.h"
#include <imgui.h>
#include <cstdio>
#include <thread>
#include <sstream>

namespace memforge {

void DrawAobScanner(App& app) {
    ImGui::SetNextWindowSize(ImVec2(750, 500), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("AOB Scanner", &app.showAobScanner)) {
        ImGui::End();
        return;
    }

    if (!app.processAttached) {
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f),
                          "No process attached.");
        ImGui::End();
        return;
    }

    // Pattern input
    ImGui::Text("Pattern:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(400);
    ImGui::InputTextWithHint("##AobPattern", "89 ?? 4C 02 00 00",
                             app.aobPatternInput, sizeof(app.aobPatternInput));

    ImGui::SameLine();

    // Scan / Cancel buttons
    bool scanning = app.aobScanner.IsScanning();
    if (scanning) {
        if (ImGui::Button("Cancel", ImVec2(80, 0))) {
            app.aobScanner.Cancel();
        }
        ImGui::SameLine();
        ImGui::ProgressBar(app.aobScanProgress, ImVec2(150, 0));
    } else {
        if (ImGui::Button("Scan", ImVec2(80, 0))) {
            std::string pattern = app.aobPatternInput;
            std::thread([&app, pattern]() {
                app.aobScanner.Scan(app.targetProcess, pattern,
                    [&app](float progress, size_t count) {
                        app.aobScanProgress = progress;
                        app.aobResultCount = count;
                    });
            }).detach();
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset")) {
            app.aobScanner.Reset();
            app.aobScanProgress = 0.0f;
            app.aobResultCount = 0;
        }
    }

    ImGui::SameLine();
    ImGui::Text("Results: %zu", app.aobScanner.GetResults().size());

    ImGui::Separator();

    // Results table
    auto& results = app.aobScanner.GetResults();
    size_t displayCount = (std::min)(results.size(), (size_t)5000);

    if (displayCount < results.size()) {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f),
                          "Showing first 5000 of %zu results.", results.size());
    }

    if (ImGui::BeginTable("AobResults", 4,
                          ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg |
                          ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable,
                          ImVec2(0, -1))) {

        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Address", ImGuiTableColumnFlags_WidthFixed, 140.0f);
        ImGui::TableSetupColumn("Matched Bytes", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 200.0f);
        ImGui::TableHeadersRow();

        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(displayCount));

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
                if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                    ImGui::SetClipboardText(addrStr);
                }

                // Matched bytes as hex string
                ImGui::TableNextColumn();
                std::string hexStr;
                for (size_t i = 0; i < r.matchedBytes.size(); i++) {
                    char hexByte[4];
                    snprintf(hexByte, sizeof(hexByte), "%02X", r.matchedBytes[i]);
                    if (i > 0) hexStr += ' ';
                    hexStr += hexByte;
                }
                ImGui::TextUnformatted(hexStr.c_str());
                if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                    ImGui::SetClipboardText(hexStr.c_str());
                }

                // Status: check if NOPed
                ImGui::TableNextColumn();
                // Read current bytes to see if they're all 0x90
                bool isNopped = false;
                if (!r.matchedBytes.empty()) {
                    std::vector<uint8_t> current(r.matchedBytes.size());
                    SIZE_T bytesRead = 0;
                    if (ReadProcessMemory(app.targetProcess,
                                          reinterpret_cast<LPCVOID>(r.address),
                                          current.data(), current.size(), &bytesRead) &&
                        bytesRead == current.size()) {
                        isNopped = true;
                        for (auto b : current) {
                            if (b != 0x90) { isNopped = false; break; }
                        }
                    }
                }
                if (isNopped) {
                    ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.2f, 1.0f), "NOPed");
                } else {
                    ImGui::TextColored(ImVec4(0.3f, 0.8f, 0.3f, 1.0f), "Active");
                }

                // Actions
                ImGui::TableNextColumn();
                ImGui::PushID(row);

                if (ImGui::SmallButton("NOP")) {
                    AobScanner::NopAt(app.targetProcess, r.address, r.matchedBytes.size());
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Restore")) {
                    app.aobScanner.RestoreAt(app.targetProcess, r.address);
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Copy")) {
                    ImGui::SetClipboardText(hexStr.c_str());
                }

                ImGui::PopID();
            }
        }

        ImGui::EndTable();
    }

    ImGui::End();
}

} // namespace memforge
