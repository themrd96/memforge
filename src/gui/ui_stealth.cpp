#include "gui/app.h"
#include "core/stealth.h"
#include <imgui.h>
#include <cstring>
#include <filesystem>

namespace memforge {

// Static state for the stealth panel
static StealthManager s_stealthMgr;
static StealthConfig s_stealthConfig;
static char s_customTitle[256] = {};
static bool s_stealthApplied = false;
static float s_detectionCheckTimer = 0.0f;
static StealthManager::DetectionStatus s_lastDetection;
static bool s_hasCheckedDetection = false;

void DrawStealth(App& app) {
    ImGui::SetNextWindowSize(ImVec2(480, 550), ImGuiCond_FirstUseEver);

    if (!ImGui::Begin("Stealth Mode", &app.showStealth)) {
        ImGui::End();
        return;
    }

    // ─── Detection Status (auto-refresh every 5s) ────────

    s_detectionCheckTimer += ImGui::GetIO().DeltaTime;
    if (s_detectionCheckTimer > 5.0f || !s_hasCheckedDetection) {
        s_lastDetection = StealthManager::CheckForDetection();
        s_detectionCheckTimer = 0.0f;
        s_hasCheckedDetection = true;
    }

    if (s_lastDetection.debuggerAttached) {
        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f),
                          "WARNING: Debugger attached to MemForge");
    }

    if (s_lastDetection.analysisToolsRunning) {
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "Detected tools:");
        for (auto& tool : s_lastDetection.detectedTools) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "%s", tool.c_str());
        }
    }

    if (!s_lastDetection.debuggerAttached && !s_lastDetection.analysisToolsRunning) {
        ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.3f, 1.0f),
                          "No detection threats found");
    }

    ImGui::Separator();
    ImGui::Spacing();

    // ─── Stealth Options ─────────────────────────────────

    ImGui::Text("Stealth Configuration");
    ImGui::Spacing();

    ImGui::Checkbox("Randomize window title", &s_stealthConfig.randomizeWindowTitle);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Changes the window title to look like a legitimate\n"
                          "Windows service. Anti-cheats often scan window titles.");
    }

    if (s_stealthConfig.randomizeWindowTitle) {
        ImGui::Indent();
        ImGui::SetNextItemWidth(300);
        ImGui::InputTextWithHint("##customtitle", "Custom title (empty = auto-generate)",
                                 s_customTitle, sizeof(s_customTitle));
        s_stealthConfig.customWindowTitle = s_customTitle;

        if (ImGui::SmallButton("Preview random name")) {
            std::string preview = NameGenerator::GenerateWindowTitle();
            strncpy(s_customTitle, preview.c_str(), sizeof(s_customTitle) - 1);
            s_stealthConfig.customWindowTitle = s_customTitle;
        }
        ImGui::Unindent();
    }

    ImGui::Spacing();

    ImGui::Checkbox("Hide from taskbar & Alt+Tab", &s_stealthConfig.hiddenFromTaskbar);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Removes MemForge from the taskbar and Alt+Tab switcher.\n"
                          "The window stays open but becomes invisible to casual observation.");
    }

    ImGui::Spacing();

    ImGui::Checkbox("Clear PE headers in memory", &s_stealthConfig.clearPeHeaders);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Wipes the executable header from MemForge's own memory.\n"
                          "Makes it harder for anti-cheats to identify the tool by\n"
                          "scanning process memory for known signatures.");
    }

    ImGui::Spacing();

    ImGui::Checkbox("Mutex cloaking", &s_stealthConfig.mutexCloaking);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Prevents creation of identifiable named mutexes.\n"
                          "Some anti-cheats check for mutexes created by known tools.");
    }

    ImGui::Spacing();

    ImGui::Checkbox("Block window enumeration", &s_stealthConfig.blockWindowEnumeration);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Uses DWM cloaking to hide from EnumWindows API.\n"
                          "Anti-cheats use EnumWindows to find cheat tool windows.\n\n"
                          "NOTE: May make the window harder to find. Use Ctrl+Shift+M.");
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // ─── Apply / Restore ─────────────────────────────────

    if (!s_stealthApplied) {
        if (ImGui::Button("Apply Stealth", ImVec2(200, 35))) {
            HWND hwnd = FindWindowA("MemForgeWindow", nullptr);
            if (!hwnd) hwnd = GetForegroundWindow();

            if (hwnd) {
                s_stealthMgr.Apply(hwnd, s_stealthConfig);
                s_stealthApplied = true;
            }
        }
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.2f, 0.2f, 1.0f));
        if (ImGui::Button("Disable Stealth", ImVec2(200, 35))) {
            s_stealthMgr.Restore();
            s_stealthApplied = false;
        }
        ImGui::PopStyleColor();

        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.3f, 1.0f), "STEALTH ACTIVE");
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // ─── Process Name Stealth ────────────────────────────

    ImGui::Text("Process Name Stealth");
    ImGui::Spacing();

    if (StealthManager::IsRunningRandomized()) {
        ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.3f, 1.0f),
                          "Running under randomized process name");
        char exePath[MAX_PATH] = {};
        GetModuleFileNameA(nullptr, exePath, MAX_PATH);
        std::filesystem::path p(exePath);
        ImGui::TextDisabled("Current name: %s", p.filename().string().c_str());
    } else {
        ImGui::TextWrapped(
            "Relaunch MemForge with a randomized executable name. "
            "Copies the exe to a temp folder with a name that looks like "
            "a Windows system process, then restarts."
        );

        ImGui::Spacing();

        static std::string previewName = NameGenerator::GenerateExeName() + ".exe";
        ImGui::TextDisabled("Example: %s", previewName.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Randomize##exename")) {
            previewName = NameGenerator::GenerateExeName() + ".exe";
        }

        ImGui::Spacing();

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.35f, 0.25f, 0.55f, 1.0f));
        if (ImGui::Button("Relaunch with Random Name", ImVec2(250, 35))) {
            if (StealthManager::RelaunchWithRandomName()) {
                PostQuitMessage(0);
            }
        }
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::TextDisabled("(will restart)");
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // ─── How it works ────────────────────────────────────

    if (ImGui::CollapsingHeader("How stealth works")) {
        ImGui::TextWrapped(
            "Anti-cheat software detects memory tools through:");
        ImGui::Spacing();
        ImGui::BulletText("Process name scanning (\"cheatengine.exe\")");
        ImGui::BulletText("Window title scanning (\"Cheat Engine 7.5\")");
        ImGui::BulletText("Mutex name detection");
        ImGui::BulletText("PE header signature matching");
        ImGui::BulletText("EnumWindows to find known tool windows");
        ImGui::Spacing();
        ImGui::TextWrapped(
            "MemForge's stealth mode counters each method by randomizing "
            "identifiable characteristics. Process rename is the most "
            "effective — it changes the actual exe name on disk."
        );
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f),
            "Kernel-level anti-cheats (EAC, BattlEye, Vanguard) use "
            "driver-level detection that these measures cannot bypass. "
            "Stealth mode works against user-mode detection only."
        );
    }

    ImGui::End();
}

} // namespace memforge
