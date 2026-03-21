#include "gui/app.h"
#include "core/lua_engine.h"
#include <imgui.h>
#include <fstream>
#include <sstream>

namespace memforge {

void DrawScriptEditor(App& app) {
    ImGui::SetNextWindowSize(ImVec2(700, 550), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Script Editor", &app.showScriptEditor)) {
        ImGui::End();
        return;
    }

    // Initialize Lua engine if needed
    if (!app.luaEngine.IsInitialized() && app.processAttached) {
        app.luaEngine.Initialize(app.targetProcess, app.targetPid);
    }

    // Toolbar
    if (ImGui::Button("Run")) {
        if (app.luaEngine.IsInitialized()) {
            auto result = app.luaEngine.Execute(app.scriptEditorText);
            if (result.success) {
                if (!result.output.empty()) {
                    app.scriptConsoleOutput += result.output;
                }
                app.scriptConsoleOutput += "[OK] Script completed.\n";
            } else {
                app.scriptConsoleOutput += "[ERROR] " + result.error + "\n";
                if (!result.output.empty()) {
                    app.scriptConsoleOutput += result.output;
                }
            }
        } else {
            app.scriptConsoleOutput += "[ERROR] Lua not initialized. Attach to a process first.\n";
        }
    }
    ImGui::SameLine();

    if (ImGui::Button("Clear Output")) {
        app.scriptConsoleOutput.clear();
    }
    ImGui::SameLine();

    if (ImGui::Button("Load Script")) {
        // Simple file dialog - just use a text input for now
        app.showScriptLoadDialog = true;
    }
    ImGui::SameLine();

    if (ImGui::Button("Save Script")) {
        app.showScriptSaveDialog = true;
    }
    ImGui::SameLine();

    if (ImGui::Button("API Reference")) {
        app.showScriptApiRef = !app.showScriptApiRef;
    }

    ImGui::Separator();

    // Split: editor on top, console on bottom
    float availH = ImGui::GetContentRegionAvail().y;
    float editorH = availH * 0.6f;
    float consoleH = availH * 0.4f - 5;

    // Script editor
    ImGui::Text("Script:");
    ImGui::InputTextMultiline("##ScriptEditor", app.scriptEditorText,
                              sizeof(app.scriptEditorText),
                              ImVec2(-1, editorH),
                              ImGuiInputTextFlags_AllowTabInput);

    ImGui::Separator();

    // Console output
    ImGui::Text("Output:");
    ImGui::BeginChild("##ScriptConsole", ImVec2(-1, consoleH), ImGuiChildFlags_Borders);
    ImGui::TextUnformatted(app.scriptConsoleOutput.c_str());
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
        ImGui::SetScrollHereY(1.0f);
    }
    ImGui::EndChild();

    // Load dialog
    if (app.showScriptLoadDialog) {
        ImGui::OpenPopup("Load Script File");
        app.showScriptLoadDialog = false;
    }
    if (ImGui::BeginPopup("Load Script File")) {
        static char loadPath[512] = {};
        ImGui::Text("File path:");
        ImGui::InputText("##LoadPath", loadPath, sizeof(loadPath));
        if (ImGui::Button("Load")) {
            std::ifstream ifs(loadPath);
            if (ifs.is_open()) {
                std::string content((std::istreambuf_iterator<char>(ifs)),
                                    std::istreambuf_iterator<char>());
                strncpy(app.scriptEditorText, content.c_str(),
                        sizeof(app.scriptEditorText) - 1);
                app.scriptEditorText[sizeof(app.scriptEditorText) - 1] = '\0';
                app.scriptConsoleOutput += "[INFO] Loaded: " + std::string(loadPath) + "\n";
            } else {
                app.scriptConsoleOutput += "[ERROR] Could not open: " + std::string(loadPath) + "\n";
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // Save dialog
    if (app.showScriptSaveDialog) {
        ImGui::OpenPopup("Save Script File");
        app.showScriptSaveDialog = false;
    }
    if (ImGui::BeginPopup("Save Script File")) {
        static char savePath[512] = {};
        ImGui::Text("File path:");
        ImGui::InputText("##SavePath", savePath, sizeof(savePath));
        if (ImGui::Button("Save")) {
            std::ofstream ofs(savePath);
            if (ofs.is_open()) {
                ofs << app.scriptEditorText;
                app.scriptConsoleOutput += "[INFO] Saved: " + std::string(savePath) + "\n";
            } else {
                app.scriptConsoleOutput += "[ERROR] Could not save: " + std::string(savePath) + "\n";
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // API Reference window
    if (app.showScriptApiRef) {
        ImGui::SetNextWindowSize(ImVec2(400, 400), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Lua API Reference", &app.showScriptApiRef)) {
            ImGui::TextWrapped("MemForge Lua API");
            ImGui::Separator();

            ImGui::Text("Memory Operations:");
            ImGui::BulletText("memforge.readInt(address)");
            ImGui::BulletText("memforge.readFloat(address)");
            ImGui::BulletText("memforge.readDouble(address)");
            ImGui::BulletText("memforge.readString(address [, maxLen])");
            ImGui::BulletText("memforge.readBytes(address, count)");
            ImGui::BulletText("memforge.writeInt(address, value)");
            ImGui::BulletText("memforge.writeFloat(address, value)");
            ImGui::BulletText("memforge.writeBytes(address, {b1, b2, ...})");

            ImGui::Spacing();
            ImGui::Text("Process Info:");
            ImGui::BulletText("memforge.getProcessId()");
            ImGui::BulletText("memforge.getModuleBase(name)");
            ImGui::BulletText("memforge.getModules()");

            ImGui::Spacing();
            ImGui::Text("Utility:");
            ImGui::BulletText("memforge.sleep(ms)");
            ImGui::BulletText("memforge.print(...) / print(...)");
        }
        ImGui::End();
    }

    ImGui::End();
}

} // namespace memforge
