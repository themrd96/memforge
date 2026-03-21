#include "gui/app.h"
#include <imgui.h>
#include <cstdio>
#include <cmath>

namespace memforge {

void DrawSpeedHack(App& app) {
    ImGui::SetNextWindowSize(ImVec2(400, 300), ImGuiCond_FirstUseEver);

    if (!ImGui::Begin("Speed Hack", &app.showSpeedHack)) {
        ImGui::End();
        return;
    }

    if (!app.processAttached) {
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f),
                          "No process attached.");
        ImGui::End();
        return;
    }

    // ─── Speed Hack Controls ────────────────────────────

    ImGui::Text("Game Speed Control");
    ImGui::Separator();
    ImGui::Spacing();

    // Enable/disable toggle
    bool wasEnabled = app.speedHackEnabled;
    if (ImGui::Checkbox("Enable Speed Hack", &app.speedHackEnabled)) {
        if (app.speedHackEnabled && !app.speedHack.IsInjected()) {
            // Inject the speedhack DLL
            if (!app.speedHack.Inject(app.targetProcess, app.targetPid)) {
                app.speedHackEnabled = false;
                ImGui::OpenPopup("Injection Failed");
            }
        }

        if (app.speedHack.IsInjected()) {
            app.speedHack.SetEnabled(app.speedHackEnabled);
        }
    }

    // Error popup
    if (ImGui::BeginPopupModal("Injection Failed", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Failed to inject speedhack DLL.");
        ImGui::Text("Make sure MemForge is running as Administrator.");
        ImGui::Spacing();
        if (ImGui::Button("OK", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::Spacing();

    // Speed slider
    if (!app.speedHackEnabled) ImGui::BeginDisabled();

    ImGui::Text("Speed Multiplier:");
    ImGui::SetNextItemWidth(-1);
    if (ImGui::SliderFloat("##speed", &app.speedValue, 0.1f, 10.0f, "%.1fx")) {
        if (app.speedHack.IsInjected()) {
            app.speedHack.SetSpeed(app.speedValue);
        }
    }

    ImGui::Spacing();

    // Preset buttons
    ImGui::Text("Presets:");
    ImGui::Spacing();

    struct SpeedPreset { const char* label; float value; };
    SpeedPreset presets[] = {
        {"0.25x", 0.25f}, {"0.5x", 0.5f}, {"1.0x", 1.0f},
        {"1.5x", 1.5f}, {"2.0x", 2.0f}, {"3.0x", 3.0f},
        {"5.0x", 5.0f}, {"10x", 10.0f}
    };

    for (int i = 0; i < IM_ARRAYSIZE(presets); i++) {
        if (i > 0) ImGui::SameLine();

        bool isActive = std::abs(app.speedValue - presets[i].value) < 0.05f;
        if (isActive) {
            ImGui::PushStyleColor(ImGuiCol_Button,
                                 ImVec4(0.35f, 0.25f, 0.55f, 1.0f));
        }

        if (ImGui::Button(presets[i].label, ImVec2(50, 30))) {
            app.speedValue = presets[i].value;
            if (app.speedHack.IsInjected()) {
                app.speedHack.SetSpeed(app.speedValue);
            }
        }

        if (isActive) {
            ImGui::PopStyleColor();
        }
    }

    if (!app.speedHackEnabled) ImGui::EndDisabled();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Status info
    ImGui::Text("Status:");
    if (app.speedHack.IsInjected()) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.3f, 1.0f), "DLL Injected");

        if (app.speedHackEnabled) {
            ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f),
                             "Running at %.1fx speed", app.speedValue);
        } else {
            ImGui::TextDisabled("Paused (1.0x)");
        }
    } else {
        ImGui::SameLine();
        ImGui::TextDisabled("Not injected");
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Info text
    ImGui::TextWrapped(
        "Speed Hack works by intercepting the game's timing functions "
        "(QueryPerformanceCounter, GetTickCount, timeGetTime) and adjusting "
        "the time values returned. This effectively speeds up or slows down "
        "all game logic that relies on system time."
    );

    ImGui::Spacing();
    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f),
                      "Note: Some games use their own timing or server-side "
                      "time checks. Speed hack may not work on all games.");

    ImGui::End();
}

} // namespace memforge
