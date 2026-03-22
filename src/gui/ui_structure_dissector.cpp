#include "gui/app.h"
#include <imgui.h>
#include <sstream>
#include <cstring>
#include <cstdlib>
#include <cmath>

namespace memforge {

void DrawStructureDissector(App& app) {
    ImGui::SetNextWindowSize(ImVec2(900, 560), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Structure Dissector", &app.showStructDissector)) {
        ImGui::End();
        return;
    }

    if (!ImGui::BeginTabBar("StructTabs")) { ImGui::End(); return; }

    // ── Tab 1: Structure Dissector ───────────────────────────────────────────
    if (!ImGui::BeginTabItem("Dissector")) { ImGui::EndTabBar(); ImGui::End(); return; }

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

            // Value (editable)
            ImGui::TableSetColumnIndex(3);
            if (app.currentStruct.baseAddress != 0 && app.processAttached) {
                std::string val = app.structDissector.FormatFieldValue(
                    app.currentStruct.baseAddress, field);

                char valBuf[128];
                strncpy(valBuf, val.c_str(), sizeof(valBuf) - 1);
                valBuf[sizeof(valBuf) - 1] = '\0';

                ImGui::SetNextItemWidth(-1);
                char valId[32];
                snprintf(valId, sizeof(valId), "##val%d", (int)i);
                if (ImGui::InputText(valId, valBuf, sizeof(valBuf),
                                     ImGuiInputTextFlags_EnterReturnsTrue)) {
                    // Write the new value to memory
                    uintptr_t writeAddr = app.currentStruct.baseAddress + field.offset;
                    // Convert based on field type
                    switch (field.type) {
                        case FieldType::Int8: {
                            int8_t v = (int8_t)atoi(valBuf);
                            app.writer.WriteProtected(writeAddr, &v, sizeof(v));
                            break;
                        }
                        case FieldType::Int16: {
                            int16_t v = (int16_t)atoi(valBuf);
                            app.writer.WriteProtected(writeAddr, &v, sizeof(v));
                            break;
                        }
                        case FieldType::Int32: {
                            int32_t v = (int32_t)atol(valBuf);
                            app.writer.WriteProtected(writeAddr, &v, sizeof(v));
                            break;
                        }
                        case FieldType::Int64: {
                            int64_t v = (int64_t)atoll(valBuf);
                            app.writer.WriteProtected(writeAddr, &v, sizeof(v));
                            break;
                        }
                        case FieldType::UInt8: {
                            uint8_t v = (uint8_t)strtoul(valBuf, nullptr, 10);
                            app.writer.WriteProtected(writeAddr, &v, sizeof(v));
                            break;
                        }
                        case FieldType::UInt16: {
                            uint16_t v = (uint16_t)strtoul(valBuf, nullptr, 10);
                            app.writer.WriteProtected(writeAddr, &v, sizeof(v));
                            break;
                        }
                        case FieldType::UInt32: {
                            uint32_t v = (uint32_t)strtoul(valBuf, nullptr, 10);
                            app.writer.WriteProtected(writeAddr, &v, sizeof(v));
                            break;
                        }
                        case FieldType::UInt64: {
                            uint64_t v = (uint64_t)strtoull(valBuf, nullptr, 10);
                            app.writer.WriteProtected(writeAddr, &v, sizeof(v));
                            break;
                        }
                        case FieldType::Float: {
                            float v = (float)atof(valBuf);
                            app.writer.WriteProtected(writeAddr, &v, sizeof(v));
                            break;
                        }
                        case FieldType::Double: {
                            double v = atof(valBuf);
                            app.writer.WriteProtected(writeAddr, &v, sizeof(v));
                            break;
                        }
                        default:
                            break;
                    }
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Type a new value and press Enter to write");
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

    ImGui::EndTabItem();

    // ── Tab 2: Nearby Search ─────────────────────────────────────────────────
    if (ImGui::BeginTabItem("Nearby Search")) {
        ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f),
            "Find values near a known address — useful for locating related fields in the same struct.");
        ImGui::Spacing();

        // Address input
        ImGui::Text("Known Address:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(180);
        ImGui::InputText("##NearAddr", app.nearbyAddrInput, sizeof(app.nearbyAddrInput),
                         ImGuiInputTextFlags_CharsHexadecimal);
        ImGui::SameLine();
        ImGui::Text("Range:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(80);
        ImGui::InputInt("##RangeBefore", &app.nearbyRangeBefore, 16);
        ImGui::SameLine();
        ImGui::Text("before /");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(80);
        ImGui::InputInt("##RangeAfter", &app.nearbyRangeAfter, 16);
        ImGui::SameLine();
        ImGui::Text("after");

        app.nearbyRangeBefore = std::max(4,  std::min(app.nearbyRangeBefore, 4096));
        app.nearbyRangeAfter  = std::max(4,  std::min(app.nearbyRangeAfter,  4096));

        // Alignment
        ImGui::Text("Step (bytes):");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(60);
        ImGui::InputInt("##Align", &app.nearbyAlignment, 1);
        app.nearbyAlignment = std::max(1, std::min(app.nearbyAlignment, 8));

        // Value filter
        ImGui::SameLine();
        ImGui::Spacing(); ImGui::SameLine();
        ImGui::Checkbox("Filter by value", &app.nearbyFilterEnabled);
        if (app.nearbyFilterEnabled) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(100);
            ImGui::InputFloat("##FilterVal", &app.nearbyFilterValue, 0, 0, "%.2f");
            ImGui::SameLine();
            ImGui::Text("± ");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(70);
            ImGui::InputFloat("##FilterTol", &app.nearbyFilterTolerance, 0, 0, "%.2f");
        }

        ImGui::Spacing();

        bool canSearch = app.processAttached && app.nearbyAddrInput[0] != '\0';
        if (!canSearch) ImGui::BeginDisabled();
        if (ImGui::Button("Search", ImVec2(100, 0))) {
            uintptr_t addr = 0;
            std::istringstream iss(app.nearbyAddrInput);
            iss >> std::hex >> addr;
            if (addr != 0) {
                app.structDissector.SetProcess(app.targetProcess);
                app.nearbyResults = app.structDissector.NearbySearch(
                    addr,
                    app.nearbyRangeBefore,
                    app.nearbyRangeAfter,
                    app.nearbyAlignment,
                    app.nearbyFilterEnabled,
                    app.nearbyFilterValue,
                    app.nearbyFilterTolerance);
            }
        }
        if (!canSearch) ImGui::EndDisabled();

        ImGui::SameLine();
        ImGui::TextDisabled("%zu results", app.nearbyResults.size());

        ImGui::Separator();

        // Results table
        if (ImGui::BeginTable("NearbyResults", 7,
                ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable,
                ImVec2(0, -30))) {

            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("Offset",   ImGuiTableColumnFlags_WidthFixed,   70);
            ImGui::TableSetupColumn("Address",  ImGuiTableColumnFlags_WidthFixed,  140);
            ImGui::TableSetupColumn("Int32",    ImGuiTableColumnFlags_WidthFixed,   80);
            ImGui::TableSetupColumn("UInt32",   ImGuiTableColumnFlags_WidthFixed,   80);
            ImGui::TableSetupColumn("Float",    ImGuiTableColumnFlags_WidthFixed,   90);
            ImGui::TableSetupColumn("Hex",      ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("##act",    ImGuiTableColumnFlags_WidthFixed,   90);
            ImGui::TableHeadersRow();

            for (auto& r : app.nearbyResults) {
                ImGui::TableNextRow();
                ImGui::PushID((int)(r.address & 0xFFFFFFFF));

                // Highlight the base address row
                bool isBase = (r.offsetFromBase == 0);
                if (isBase)
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0,
                        ImGui::GetColorU32(ImVec4(0.2f, 0.5f, 0.2f, 0.4f)));

                // Offset
                ImGui::TableSetColumnIndex(0);
                if (r.offsetFromBase >= 0)
                    ImGui::Text("+0x%X", r.offsetFromBase);
                else
                    ImGui::Text("-0x%X", -r.offsetFromBase);

                // Address
                ImGui::TableSetColumnIndex(1);
                char addrStr[32];
                snprintf(addrStr, sizeof(addrStr), "0x%llX", (unsigned long long)r.address);
                ImGui::TextUnformatted(addrStr);
                if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
                    ImGui::SetClipboardText(addrStr);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Right-click to copy address");

                // Int32
                ImGui::TableSetColumnIndex(2);
                ImGui::Text("%d", r.asInt32);

                // UInt32
                ImGui::TableSetColumnIndex(3);
                ImGui::Text("%u", r.asUInt32);

                // Float
                ImGui::TableSetColumnIndex(4);
                if (std::isfinite(r.asFloat))
                    ImGui::Text("%.3f", r.asFloat);
                else
                    ImGui::TextDisabled("NaN/Inf");

                // Hex
                ImGui::TableSetColumnIndex(5);
                ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f),
                    "%02X %02X %02X %02X %02X %02X %02X %02X",
                    r.rawBytes[0], r.rawBytes[1], r.rawBytes[2], r.rawBytes[3],
                    r.rawBytes[4], r.rawBytes[5], r.rawBytes[6], r.rawBytes[7]);

                // Action buttons
                ImGui::TableSetColumnIndex(6);
                if (ImGui::SmallButton("-> Struct")) {
                    // Add to the struct definition at the correct offset
                    size_t fieldOffset = (r.offsetFromBase >= 0)
                        ? (size_t)r.offsetFromBase : 0;
                    app.currentStruct.AddField(
                        "nearby_" + std::to_string(r.offsetFromBase),
                        std::isfinite(r.asFloat) ? FieldType::Float : FieldType::Int32,
                        fieldOffset);
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Freeze")) {
                    ScanValue sv{};
                    sv.int32Val = r.asInt32;
                    app.freezer.AddEntry(r.address, sv, ValueType::Int32,
                        "nearby @" + std::string(addrStr));
                }

                ImGui::PopID();
            }

            ImGui::EndTable();
        }

        ImGui::EndTabItem();
    }

    ImGui::EndTabBar();
    ImGui::End();
}

} // namespace memforge
