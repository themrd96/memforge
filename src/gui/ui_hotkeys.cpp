#include "gui/app.h"
#include "core/hotkey_manager.h"
#include <imgui.h>
#include <cstdio>

namespace memforge {

static const char* actionNames[] = {
    "Toggle Freeze", "Run Script", "Set Speed", "Toggle Speed Hack", "Custom"
};

static const char* commonKeyNames[] = {
    "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12",
    "Num0", "Num1", "Num2", "Num3", "Num4", "Num5", "Num6", "Num7", "Num8", "Num9",
    "Insert", "Delete", "Home", "End", "PageUp", "PageDown"
};

static const int commonKeyCodes[] = {
    VK_F1, VK_F2, VK_F3, VK_F4, VK_F5, VK_F6, VK_F7, VK_F8, VK_F9, VK_F10, VK_F11, VK_F12,
    VK_NUMPAD0, VK_NUMPAD1, VK_NUMPAD2, VK_NUMPAD3, VK_NUMPAD4, VK_NUMPAD5,
    VK_NUMPAD6, VK_NUMPAD7, VK_NUMPAD8, VK_NUMPAD9,
    VK_INSERT, VK_DELETE, VK_HOME, VK_END, VK_PRIOR, VK_NEXT
};

void DrawHotkeys(App& app) {
    ImGui::SetNextWindowSize(ImVec2(700, 450), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Hotkeys", &app.showHotkeys)) {
        ImGui::End();
        return;
    }

    // Add hotkey button
    if (ImGui::Button("Add Hotkey")) {
        ImGui::OpenPopup("AddHotkeyPopup");
    }

    // Add hotkey popup
    if (ImGui::BeginPopup("AddHotkeyPopup")) {
        static int selectedKey = 0;
        static bool hkCtrl = false;
        static bool hkAlt = false;
        static bool hkShift = false;
        static int selectedAction = 0;
        static char hkDesc[256] = {};
        static int hkFreezeId = -1;
        static char hkScript[4096] = {};
        static float hkSpeed = 2.0f;
        static bool capturingKey = false;
        static int capturedVk = 0;

        ImGui::Text("Key:");
        ImGui::SameLine();

        // Key capture mode
        if (capturingKey) {
            ImGui::Button("Press any key...", ImVec2(200, 0));
            // Check for key presses
            for (int vk = 0x01; vk <= 0xFE; vk++) {
                if (vk == VK_CONTROL || vk == VK_MENU || vk == VK_SHIFT ||
                    vk == VK_LCONTROL || vk == VK_RCONTROL ||
                    vk == VK_LMENU || vk == VK_RMENU ||
                    vk == VK_LSHIFT || vk == VK_RSHIFT ||
                    vk == VK_LBUTTON || vk == VK_RBUTTON || vk == VK_MBUTTON)
                    continue;
                if (GetAsyncKeyState(vk) & 0x8000) {
                    capturedVk = vk;
                    capturingKey = false;
                    // Find in common keys list
                    selectedKey = -1;
                    for (int i = 0; i < IM_ARRAYSIZE(commonKeyCodes); i++) {
                        if (commonKeyCodes[i] == vk) {
                            selectedKey = i;
                            break;
                        }
                    }
                    break;
                }
            }
        } else {
            if (ImGui::Button("Capture Key", ImVec2(120, 0))) {
                capturingKey = true;
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(120);
            if (ImGui::Combo("##KeySelect", &selectedKey, commonKeyNames, IM_ARRAYSIZE(commonKeyNames))) {
                capturedVk = commonKeyCodes[selectedKey];
            }
        }

        ImGui::Checkbox("Ctrl", &hkCtrl);
        ImGui::SameLine();
        ImGui::Checkbox("Alt", &hkAlt);
        ImGui::SameLine();
        ImGui::Checkbox("Shift", &hkShift);

        ImGui::SetNextItemWidth(200);
        ImGui::Combo("Action", &selectedAction, actionNames, IM_ARRAYSIZE(actionNames));

        // Action-specific settings
        if (selectedAction == 0) { // Toggle Freeze
            ImGui::SetNextItemWidth(100);
            ImGui::InputInt("Freeze ID", &hkFreezeId);
        } else if (selectedAction == 1) { // Run Script
            ImGui::InputTextMultiline("##Script", hkScript, sizeof(hkScript),
                                      ImVec2(300, 80));
        } else if (selectedAction == 2) { // Set Speed
            ImGui::SetNextItemWidth(100);
            ImGui::InputFloat("Speed", &hkSpeed, 0.1f, 1.0f, "%.1f");
        }

        ImGui::SetNextItemWidth(300);
        ImGui::InputText("Description", hkDesc, sizeof(hkDesc));

        if (ImGui::Button("Add", ImVec2(80, 0))) {
            if (capturedVk != 0 || selectedKey >= 0) {
                Hotkey hk;
                hk.vkCode = (capturedVk != 0) ? capturedVk :
                             (selectedKey >= 0 ? commonKeyCodes[selectedKey] : VK_F1);
                hk.ctrl = hkCtrl;
                hk.alt = hkAlt;
                hk.shift = hkShift;
                hk.action = static_cast<HotkeyAction>(selectedAction);
                hk.description = hkDesc;
                hk.freezeId = hkFreezeId;
                hk.scriptCode = hkScript;
                hk.speedValue = hkSpeed;
                hk.enabled = true;
                app.hotkeyManager.AddHotkey(hk);

                // Reset
                hkDesc[0] = '\0';
                hkScript[0] = '\0';
                capturedVk = 0;
                selectedKey = 0;
                hkCtrl = false;
                hkAlt = false;
                hkShift = false;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(80, 0))) {
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    ImGui::Separator();

    // Hotkeys table
    auto& hotkeys = app.hotkeyManager.GetHotkeys();

    if (ImGui::BeginTable("HotkeysTable", 5,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                          ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY,
                          ImVec2(0, -1))) {

        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthFixed, 140.0f);
        ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, 130.0f);
        ImGui::TableSetupColumn("Description", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Enabled", ImGuiTableColumnFlags_WidthFixed, 60.0f);
        ImGui::TableSetupColumn("Remove", ImGuiTableColumnFlags_WidthFixed, 60.0f);
        ImGui::TableHeadersRow();

        int removeId = -1;
        for (int i = 0; i < static_cast<int>(hotkeys.size()); i++) {
            auto& hk = hotkeys[i];
            ImGui::TableNextRow();
            ImGui::PushID(hk.id);

            // Key combo
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(hk.GetKeyString().c_str());

            // Action
            ImGui::TableNextColumn();
            int actionIdx = static_cast<int>(hk.action);
            if (actionIdx >= 0 && actionIdx < IM_ARRAYSIZE(actionNames)) {
                ImGui::TextUnformatted(actionNames[actionIdx]);
            }

            // Description
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(hk.description.c_str());

            // Enabled toggle
            ImGui::TableNextColumn();
            bool enabled = hk.enabled;
            if (ImGui::Checkbox("##en", &enabled)) {
                hk.enabled = enabled;
                // Re-register
                app.hotkeyManager.UnregisterAll();
                app.hotkeyManager.RegisterAll();
            }

            // Remove button
            ImGui::TableNextColumn();
            if (ImGui::SmallButton("X")) {
                removeId = hk.id;
            }

            ImGui::PopID();
        }

        ImGui::EndTable();

        if (removeId >= 0) {
            app.hotkeyManager.RemoveHotkey(removeId);
        }
    }

    if (hotkeys.empty()) {
        ImGui::TextDisabled("No hotkeys configured.\nClick 'Add Hotkey' to create one.");
    }

    ImGui::End();
}

} // namespace memforge
