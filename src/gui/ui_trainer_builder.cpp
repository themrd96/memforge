#include "gui/app.h"
#include "core/trainer_builder.h"
#include <imgui.h>
#include <cstdio>
#include <cstring>

namespace memforge {

static const char* cheatTypeNames[] = {
    "Freeze Value", "Set Value", "NOP Bytes", "Run Script"
};

static const char* valueTypeNames_tb[] = {
    "int32", "float", "double", "int64"
};

void DrawTrainerBuilder(App& app) {
    ImGui::SetNextWindowSize(ImVec2(800, 600), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Trainer Builder", &app.showTrainerBuilder)) {
        ImGui::End();
        return;
    }

    // Game config
    ImGui::Text("Game Configuration");
    ImGui::Separator();

    ImGui::SetNextItemWidth(300);
    ImGui::InputText("Game Name", app.trainerGameName, sizeof(app.trainerGameName));

    ImGui::SetNextItemWidth(300);
    ImGui::InputText("Game Executable", app.trainerGameExe, sizeof(app.trainerGameExe));

    ImGui::SetNextItemWidth(300);
    ImGui::InputText("Trainer Name", app.trainerName, sizeof(app.trainerName));

    ImGui::SetNextItemWidth(300);
    ImGui::InputText("Author", app.trainerAuthor, sizeof(app.trainerAuthor));

    ImGui::Spacing();
    ImGui::Text("Cheats");
    ImGui::Separator();

    // Add cheat button
    if (ImGui::Button("Add Cheat")) {
        TrainerCheat cheat;
        cheat.name = "New Cheat";
        cheat.type = TrainerCheat::CheatType::FreezeValue;
        cheat.valueType = "int32";
        cheat.value = "999999";
        app.trainerCheats.push_back(cheat);
    }

    // Cheats list
    int removeIdx = -1;
    for (int i = 0; i < static_cast<int>(app.trainerCheats.size()); i++) {
        auto& cheat = app.trainerCheats[i];
        ImGui::PushID(i);

        bool open = ImGui::TreeNode("##cheat", "[%d] %s", i + 1, cheat.name.c_str());

        ImGui::SameLine();
        if (ImGui::SmallButton("Remove")) {
            removeIdx = i;
        }

        if (open) {
            // Name
            char nameBuf[256] = {};
            strncpy(nameBuf, cheat.name.c_str(), sizeof(nameBuf) - 1);
            ImGui::SetNextItemWidth(250);
            if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf))) {
                cheat.name = nameBuf;
            }

            // Description
            char descBuf[512] = {};
            strncpy(descBuf, cheat.description.c_str(), sizeof(descBuf) - 1);
            ImGui::SetNextItemWidth(250);
            if (ImGui::InputText("Description", descBuf, sizeof(descBuf))) {
                cheat.description = descBuf;
            }

            // Hotkey VK code
            ImGui::SetNextItemWidth(100);
            ImGui::InputInt("Hotkey VK", &cheat.hotkeyVk);
            ImGui::SameLine();
            ImGui::Checkbox("Ctrl", &cheat.hotkeyCtrl);
            ImGui::SameLine();
            ImGui::Checkbox("Alt", &cheat.hotkeyAlt);

            // Cheat type
            int typeIdx = static_cast<int>(cheat.type);
            ImGui::SetNextItemWidth(150);
            if (ImGui::Combo("Type", &typeIdx, cheatTypeNames, IM_ARRAYSIZE(cheatTypeNames))) {
                cheat.type = static_cast<TrainerCheat::CheatType>(typeIdx);
            }

            // Type-specific fields
            if (cheat.type == TrainerCheat::CheatType::FreezeValue ||
                cheat.type == TrainerCheat::CheatType::SetValue) {

                char aobBuf[512] = {};
                strncpy(aobBuf, cheat.aobPattern.c_str(), sizeof(aobBuf) - 1);
                ImGui::SetNextItemWidth(300);
                if (ImGui::InputTextWithHint("AOB Pattern", "89 ?? 4C 02", aobBuf, sizeof(aobBuf))) {
                    cheat.aobPattern = aobBuf;
                }

                ImGui::SetNextItemWidth(100);
                ImGui::InputInt("AOB Offset", &cheat.aobOffset);

                // Value type dropdown
                int vtIdx = 0;
                for (int v = 0; v < IM_ARRAYSIZE(valueTypeNames_tb); v++) {
                    if (cheat.valueType == valueTypeNames_tb[v]) { vtIdx = v; break; }
                }
                ImGui::SetNextItemWidth(100);
                if (ImGui::Combo("Value Type", &vtIdx, valueTypeNames_tb, IM_ARRAYSIZE(valueTypeNames_tb))) {
                    cheat.valueType = valueTypeNames_tb[vtIdx];
                }

                char valBuf[128] = {};
                strncpy(valBuf, cheat.value.c_str(), sizeof(valBuf) - 1);
                ImGui::SetNextItemWidth(150);
                if (ImGui::InputText("Value", valBuf, sizeof(valBuf))) {
                    cheat.value = valBuf;
                }
            } else if (cheat.type == TrainerCheat::CheatType::NopBytes) {
                char aobBuf[512] = {};
                strncpy(aobBuf, cheat.aobPattern.c_str(), sizeof(aobBuf) - 1);
                ImGui::SetNextItemWidth(300);
                if (ImGui::InputTextWithHint("AOB Pattern", "89 ?? 4C 02", aobBuf, sizeof(aobBuf))) {
                    cheat.aobPattern = aobBuf;
                }

                ImGui::SetNextItemWidth(100);
                ImGui::InputInt("NOP Count", &cheat.nopCount);
            } else if (cheat.type == TrainerCheat::CheatType::RunScript) {
                char scriptBuf[4096] = {};
                strncpy(scriptBuf, cheat.luaScript.c_str(), sizeof(scriptBuf) - 1);
                if (ImGui::InputTextMultiline("Script", scriptBuf, sizeof(scriptBuf),
                                              ImVec2(400, 100))) {
                    cheat.luaScript = scriptBuf;
                }
            }

            ImGui::TreePop();
        }

        ImGui::PopID();
    }

    if (removeIdx >= 0 && removeIdx < static_cast<int>(app.trainerCheats.size())) {
        app.trainerCheats.erase(app.trainerCheats.begin() + removeIdx);
    }

    if (app.trainerCheats.empty()) {
        ImGui::TextDisabled("No cheats defined. Click 'Add Cheat' to add one.");
    }

    ImGui::Spacing();
    ImGui::Separator();

    // Output path
    ImGui::SetNextItemWidth(400);
    ImGui::InputText("Output Path", app.trainerOutputPath, sizeof(app.trainerOutputPath));

    // Generate / Build buttons
    if (ImGui::Button("Generate Source", ImVec2(150, 30))) {
        TrainerConfig cfg;
        cfg.gameName = app.trainerGameName;
        cfg.gameExe = app.trainerGameExe;
        cfg.trainerName = app.trainerName;
        cfg.author = app.trainerAuthor;
        cfg.cheats = app.trainerCheats;

        std::string outPath = app.trainerOutputPath;
        if (outPath.empty()) outPath = "trainer.cpp";

        if (TrainerBuilder::GenerateTrainerSource(cfg, outPath)) {
            app.trainerBuildLog = "Source generated: " + outPath;
            TrainerBuilder::GenerateBuildScript(outPath, outPath.substr(0, outPath.rfind('.')) + ".exe");
        } else {
            app.trainerBuildLog = "ERROR: Failed to generate source file.";
        }
    }

    ImGui::SameLine();

    if (ImGui::Button("Build Trainer", ImVec2(150, 30))) {
        TrainerConfig cfg;
        cfg.gameName = app.trainerGameName;
        cfg.gameExe = app.trainerGameExe;
        cfg.trainerName = app.trainerName;
        cfg.author = app.trainerAuthor;
        cfg.cheats = app.trainerCheats;

        std::string outPath = app.trainerOutputPath;
        if (outPath.empty()) outPath = "trainer.exe";

        app.trainerBuildLog = "Building...";
        if (TrainerBuilder::BuildTrainer(cfg, outPath)) {
            app.trainerBuildLog = "Build succeeded: " + outPath;
        } else {
            app.trainerBuildLog = "Build failed. Make sure cl.exe is in PATH (run from VS Developer Command Prompt).";
        }
    }

    // Build log
    if (!app.trainerBuildLog.empty()) {
        ImGui::Spacing();
        bool isError = app.trainerBuildLog.find("ERROR") != std::string::npos ||
                       app.trainerBuildLog.find("failed") != std::string::npos;
        if (isError) {
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.2f, 1.0f), "%s", app.trainerBuildLog.c_str());
        } else {
            ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.3f, 1.0f), "%s", app.trainerBuildLog.c_str());
        }
    }

    ImGui::End();
}

} // namespace memforge
