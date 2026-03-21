#include "gui/app.h"
#include <imgui.h>
#include <sstream>
#include <cstring>

namespace memforge {

void DrawStructureDissector(App& app) {
    ImGui::SetNextWindowSize(ImVec2(800, 500), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Structure Dissector", &app.showStructDissector)) {
        ImGui::End();
        return;
    }

    // Address input
    ImGui::Text("Base Address:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(200);
    ImGui::InputText("##StructAddr", app.structAddrInput, sizeof(app.structAddrInput),
                     ImGuiInputTextFlags_CharsHexadecimal);
    ImGui::SameLine();

    if (ImGui::Button("Read")) {
        uintptr_t addr = 0;
        std::istringstream iss(app.structAddrInput);
        iss >> std::hex >> addr;
        if (addr != 0) {
            app.currentStruct.baseAddress = addr;
            if (app.currentStruct.fields.empty()) {
                // Auto-detect first time
                app.structDissector.SetProcess(app.targetProcess);
                app.currentStruct = app.structDissector.AutoDetect(addr, 256);
            }
        }
    }
    ImGui::SameLine();

    if (ImGui::Button("Auto-Detect")) {
        uintptr_t addr = 0;
        std::istringstream iss(app.structAddrInput);
        iss >> std::hex >> addr;
        if (addr != 0) {
            app.structDissector.SetProcess(app.targetProcess);
            app.currentStruct = app.structDissector.AutoDetect(addr, 256);
        }
    }

    // Structure name
    ImGui::SameLine();
    ImGui::Text("Name:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(150);
    char nameBuf[128] = {};
    strncpy(nameBuf, app.currentStruct.name.c_str(), sizeof(nameBuf) - 1);
    if (ImGui::InputText("##StructName", nameBuf, sizeof(nameBuf))) {
        app.currentStruct.name = nameBuf;
    }

    ImGui::Separator();

    // Buttons
    if (ImGui::Button("Add Field")) {
        size_t nextOffset = app.currentStruct.GetTotalSize();
        app.currentStruct.AddField("new_field", FieldType::Int32, nextOffset);
    }
    ImGui::SameLine();

    if (ImGui::Button("Generate C++")) {
        std::string code = app.currentStruct.GenerateCppStruct();
        ImGui::SetClipboardText(code.c_str());
    }
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "(copied to clipboard)");

    ImGui::Separator();

    // Fields table
    if (ImGui::BeginTable("StructFields", 6,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                          ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY,
                          ImVec2(0, -30))) {
        ImGui::TableSetupColumn("Offset", ImGuiTableColumnFlags_WidthFixed, 80);
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 100);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 150);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Hex", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("##Actions", ImGuiTableColumnFlags_WidthFixed, 30);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();

        app.structDissector.SetProcess(app.targetProcess);

        int removeIdx = -1;
        for (size_t i = 0; i < app.currentStruct.fields.size(); i++) {
            auto& field = app.currentStruct.fields[i];
            ImGui::TableNextRow();
            ImGui::PushID(static_cast<int>(i));

            // Offset
            ImGui::TableSetColumnIndex(0);
            char offsetStr[32];
            snprintf(offsetStr, sizeof(offsetStr), "0x%04zX", field.offset);
            ImGui::TextUnformatted(offsetStr);
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                ImGui::SetClipboardText(offsetStr);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Right-click to copy");
            }

            // Type (combo)
            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(-1);
            static const char* typeNames[] = {
                "Int8", "Int16", "Int32", "Int64",
                "UInt8", "UInt16", "UInt32", "UInt64",
                "Float", "Double", "Pointer", "String", "Padding"
            };
            int currentType = static_cast<int>(field.type);
            if (ImGui::Combo("##Type", &currentType, typeNames, IM_ARRAYSIZE(typeNames))) {
                field.type = static_cast<FieldType>(currentType);
                field.size = StructDefinition::GetFieldSize(field.type);
            }

            // Name (editable)
            ImGui::TableSetColumnIndex(2);
            char fieldName[128] = {};
            strncpy(fieldName, field.name.c_str(), sizeof(fieldName) - 1);
            ImGui::SetNextItemWidth(-1);
            if (ImGui::InputText("##Name", fieldName, sizeof(fieldName))) {
                field.name = fieldName;
            }

            // Value (live-read)
            ImGui::TableSetColumnIndex(3);
            if (app.currentStruct.baseAddress != 0 && app.processAttached) {
                std::string val = app.structDissector.FormatFieldValue(
                    app.currentStruct.baseAddress, field);
                ImGui::TextUnformatted(val.c_str());
                if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                    ImGui::SetClipboardText(val.c_str());
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Right-click to copy");
                }
            } else {
                ImGui::TextDisabled("N/A");
            }

            // Hex
            ImGui::TableSetColumnIndex(4);
            if (app.currentStruct.baseAddress != 0 && app.processAttached) {
                std::string hex = app.structDissector.FormatFieldHex(
                    app.currentStruct.baseAddress, field);
                ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "%s", hex.c_str());
                if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                    ImGui::SetClipboardText(hex.c_str());
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Right-click to copy");
                }
            } else {
                ImGui::TextDisabled("--");
            }

            // Remove button
            ImGui::TableSetColumnIndex(5);
            if (ImGui::SmallButton("X")) {
                removeIdx = static_cast<int>(i);
            }

            ImGui::PopID();
        }

        ImGui::EndTable();

        if (removeIdx >= 0) {
            app.currentStruct.RemoveField(static_cast<size_t>(removeIdx));
        }
    }

    // Status
    ImGui::Text("Fields: %zu | Total Size: 0x%zX",
                app.currentStruct.fields.size(), app.currentStruct.GetTotalSize());

    ImGui::End();
}

} // namespace memforge
