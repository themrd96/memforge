#include "gui/app.h"
#include "core/anti_anticheat.h"
#include <imgui.h>
#include <cstring>

namespace memforge {

static char s_dllPath[512] = {};
static std::string s_errorLog;

void DrawAntiCheat(App& app) {
    ImGui::SetNextWindowSize(ImVec2(550, 600), ImGuiCond_FirstUseEver);

    if (!ImGui::Begin("Anti-Anti-Cheat", &app.showAntiCheat)) {
        ImGui::End();
        return;
    }

    auto& status = AntiAntiCheat::GetStatus();

    // ─── Warning ─────────────────────────────────────────────

    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f),
        "WARNING: These techniques bypass user-mode anti-cheat only.");
    ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.0f, 1.0f),
        "Kernel anti-cheats (EAC, BattlEye, Vanguard) use driver-level");
    ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.0f, 1.0f),
        "detection that cannot be bypassed from user mode.");

    ImGui::Separator();
    ImGui::Spacing();

    // ─── Technique Toggles ───────────────────────────────────

    auto drawStatusIndicator = [](bool active, bool failed = false) {
        if (failed) {
            ImGui::TextColored(ImVec4(0.9f, 0.2f, 0.2f, 1.0f), "[FAIL]");
        } else if (active) {
            ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.3f, 1.0f), "[ON]");
        } else {
            ImGui::TextDisabled("[OFF]");
        }
    };

    // Direct Syscalls
    ImGui::Text("Direct Syscall Stubs");
    ImGui::SameLine(400);
    drawStatusIndicator(status.syscallStubsActive);
    ImGui::TextDisabled("  Bypass ntdll hooks by calling syscall instruction directly.");
    ImGui::TextDisabled("  Anti-cheats hook NtRead/WriteVirtualMemory in ntdll.dll.");
    if (ImGui::Button("Enable Syscalls", ImVec2(160, 28))) {
        if (!AntiAntiCheat::UseSyscallStubs()) {
            s_errorLog += "Syscall stubs: " + status.lastError + "\n";
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Hide Handle
    ImGui::Text("Hide Handle");
    ImGui::SameLine(400);
    drawStatusIndicator(status.handleHidden);
    ImGui::TextDisabled("  Modify handle attributes to hide from enumeration.");
    ImGui::TextDisabled("  Prevents anti-cheat from detecting our process handle.");
    {
        bool attached = app.processAttached && app.targetProcess;
        ImGui::BeginDisabled(!attached);
        if (ImGui::Button("Hide Handle", ImVec2(160, 28))) {
            if (!AntiAntiCheat::HideHandle(app.targetProcess)) {
                s_errorLog += "Hide handle: " + status.lastError + "\n";
            }
        }
        ImGui::EndDisabled();
        if (!attached) {
            ImGui::SameLine();
            ImGui::TextDisabled("(attach to process first)");
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Unlink From PEB
    ImGui::Text("Unlink From PEB");
    ImGui::SameLine(400);
    drawStatusIndicator(status.pebUnlinked);
    ImGui::TextDisabled("  Remove MemForge from the PEB module list.");
    ImGui::TextDisabled("  Anti-cheats walk the PEB to find cheat tool modules.");
    if (ImGui::Button("Unlink PEB", ImVec2(160, 28))) {
        if (!AntiAntiCheat::UnlinkFromPEB()) {
            s_errorLog += "PEB unlink: " + status.lastError + "\n";
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Hide Thread
    ImGui::Text("Hide Thread From Debugger");
    ImGui::SameLine(400);
    drawStatusIndicator(status.threadHidden);
    ImGui::TextDisabled("  NtSetInformationThread with ThreadHideFromDebugger.");
    ImGui::TextDisabled("  Hides the current thread from debugger enumeration.");
    if (ImGui::Button("Hide Thread", ImVec2(160, 28))) {
        HANDLE hThread = GetCurrentThread();
        if (!AntiAntiCheat::HideThread(hThread)) {
            s_errorLog += "Hide thread: " + status.lastError + "\n";
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Manual Map DLL
    ImGui::Text("Manual Map DLL");
    ImGui::SameLine(400);
    drawStatusIndicator(status.manualMapAvailable);
    ImGui::TextDisabled("  Inject a DLL without LoadLibrary (no PEB registration).");
    ImGui::TextDisabled("  Maps PE sections and resolves imports manually.");

    ImGui::SetNextItemWidth(350);
    ImGui::InputTextWithHint("##dllpath", "Path to DLL...", s_dllPath, sizeof(s_dllPath));
    {
        bool attached = app.processAttached && app.targetProcess;
        bool hasPath = s_dllPath[0] != '\0';
        ImGui::BeginDisabled(!attached || !hasPath);
        if (ImGui::Button("Map DLL", ImVec2(160, 28))) {
            if (!AntiAntiCheat::ManualMapDll(app.targetProcess, s_dllPath)) {
                s_errorLog += "Manual map: " + status.lastError + "\n";
            }
        }
        ImGui::EndDisabled();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // ─── Apply All / Remove All ──────────────────────────────

    if (ImGui::Button("Apply All", ImVec2(120, 32))) {
        AntiAntiCheat::UseSyscallStubs();
        AntiAntiCheat::UnlinkFromPEB();
        HANDLE hThread = GetCurrentThread();
        AntiAntiCheat::HideThread(hThread);
        if (app.processAttached && app.targetProcess) {
            AntiAntiCheat::HideHandle(app.targetProcess);
        }
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(applies all non-DLL techniques)");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // ─── Error Log ───────────────────────────────────────────

    if (ImGui::CollapsingHeader("Error Log")) {
        if (s_errorLog.empty()) {
            ImGui::TextDisabled("No errors.");
        } else {
            ImGui::TextWrapped("%s", s_errorLog.c_str());
            if (ImGui::SmallButton("Clear Log")) {
                s_errorLog.clear();
            }
        }
    }

    ImGui::End();
}

} // namespace memforge
