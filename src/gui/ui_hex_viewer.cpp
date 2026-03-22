#include "gui/app.h"
#include <imgui.h>
#include <cstdio>
#include <cstring>
#include <cctype>
#include <vector>

namespace memforge {

void DrawHexViewer(App& app) {
    ImGui::SetNextWindowSize(ImVec2(700, 500), ImGuiCond_FirstUseEver);

    if (!ImGui::Begin("Hex Viewer", &app.showHexViewer)) {
        ImGui::End();
        return;
    }

    if (!app.processAttached) {
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f),
                          "No process attached.");
        ImGui::End();
        return;
    }

    // Address input
    ImGui::SetNextItemWidth(200);
    if (ImGui::InputText("Address (hex)", app.hexAddrInput, sizeof(app.hexAddrInput),
                         ImGuiInputTextFlags_CharsHexadecimal |
                         ImGuiInputTextFlags_EnterReturnsTrue)) {
        unsigned long long addr = 0;
        if (sscanf(app.hexAddrInput, "%llx", &addr) == 1) {
            app.hexViewAddress = static_cast<uintptr_t>(addr);
        }
    }

    ImGui::SameLine();
    if (ImGui::Button("Go")) {
        unsigned long long addr = 0;
        if (sscanf(app.hexAddrInput, "%llx", &addr) == 1) {
            app.hexViewAddress = static_cast<uintptr_t>(addr);
        }
    }

    ImGui::SameLine();
    ImGui::SetNextItemWidth(80);
    ImGui::InputInt("Columns", &app.hexViewColumns);
    app.hexViewColumns = std::max(4, std::min(32, app.hexViewColumns));

    ImGui::SameLine();
    ImGui::SetNextItemWidth(80);
    ImGui::InputInt("Rows", &app.hexViewRows);
    app.hexViewRows = std::max(4, std::min(128, app.hexViewRows));

    // Navigation
    ImGui::SameLine();
    size_t pageSize = app.hexViewColumns * app.hexViewRows;
    if (ImGui::ArrowButton("##up", ImGuiDir_Up)) {
        if (app.hexViewAddress >= pageSize)
            app.hexViewAddress -= pageSize;
        else
            app.hexViewAddress = 0;
        snprintf(app.hexAddrInput, sizeof(app.hexAddrInput),
                "%llX", (unsigned long long)app.hexViewAddress);
    }
    ImGui::SameLine();
    if (ImGui::ArrowButton("##down", ImGuiDir_Down)) {
        app.hexViewAddress += pageSize;
        snprintf(app.hexAddrInput, sizeof(app.hexAddrInput),
                "%llX", (unsigned long long)app.hexViewAddress);
    }

    ImGui::Separator();

    // Issue 17: Cache the last read buffer; only call ReadProcessMemory when
    // address or dimensions change, or after a throttle interval (~16 frames).
    size_t totalBytes = app.hexViewColumns * app.hexViewRows;

    bool needsRefresh = false;
    if (app.hexViewAddress   != app.hexCacheAddress  ||
        app.hexViewColumns   != app.hexCacheColumns  ||
        app.hexViewRows      != app.hexCacheRows) {
        needsRefresh = true;
    } else {
        app.hexCacheFrameCounter++;
        if (app.hexCacheFrameCounter >= 16) {
            needsRefresh = true;
            app.hexCacheFrameCounter = 0;
        }
    }

    if (needsRefresh) {
        app.hexCacheBuffer = app.writer.ReadBytes(app.hexViewAddress, totalBytes);
        app.hexCacheAddress = app.hexViewAddress;
        app.hexCacheColumns = app.hexViewColumns;
        app.hexCacheRows    = app.hexViewRows;
    }

    const std::vector<uint8_t>& data = app.hexCacheBuffer;

    if (data.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f),
                          "Cannot read memory at 0x%llX",
                          (unsigned long long)app.hexViewAddress);
        ImGui::End();
        return;
    }

    // Draw hex view with monospace font
    ImGui::BeginChild("HexView", ImVec2(0, 0), ImGuiChildFlags_None,
                      ImGuiWindowFlags_HorizontalScrollbar);

    // Header
    ImGui::TextDisabled("          ");
    for (int col = 0; col < app.hexViewColumns; col++) {
        ImGui::SameLine();
        ImGui::TextDisabled("%02X", col);
        if (col < app.hexViewColumns - 1) ImGui::SameLine();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("  ASCII");

    ImGui::Separator();

    for (int row = 0; row < app.hexViewRows; row++) {
        size_t rowOffset = row * app.hexViewColumns;
        if (rowOffset >= data.size()) break;

        uintptr_t rowAddr = app.hexViewAddress + rowOffset;

        // Address
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.8f, 1.0f),
                          "%08llX:", (unsigned long long)rowAddr);

        // Hex bytes
        for (int col = 0; col < app.hexViewColumns; col++) {
            ImGui::SameLine();
            size_t idx = rowOffset + col;
            if (idx < data.size()) {
                uint8_t byte = data[idx];

                // Color code: zero = dim, printable = normal, other = yellow
                ImVec4 color;
                if (byte == 0) {
                    color = ImVec4(0.35f, 0.35f, 0.35f, 1.0f);
                } else if (byte >= 32 && byte < 127) {
                    color = ImVec4(0.9f, 0.9f, 0.9f, 1.0f);
                } else {
                    color = ImVec4(0.9f, 0.75f, 0.3f, 1.0f);
                }

                ImGui::TextColored(color, "%02X", byte);
            } else {
                ImGui::TextDisabled("  ");
            }
        }

        // ASCII representation
        ImGui::SameLine();
        ImGui::TextDisabled(" |");
        ImGui::SameLine();

        char asciiLine[128] = {};
        for (int col = 0; col < app.hexViewColumns && (rowOffset + col) < data.size(); col++) {
            uint8_t byte = data[rowOffset + col];
            asciiLine[col] = (byte >= 32 && byte < 127) ? (char)byte : '.';
        }
        ImGui::TextUnformatted(asciiLine);
    }

    ImGui::EndChild();
    ImGui::End();
}

} // namespace memforge
