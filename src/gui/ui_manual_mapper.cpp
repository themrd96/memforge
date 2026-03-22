#include "gui/app.h"
#include <imgui.h>
#include <shobjidl.h>
#include <comdef.h>
#include <cstdio>
#include <cstring>

namespace memforge {

static std::string BrowseForDll(HWND owner) {
    std::string result;
    IFileOpenDialog* pDlg = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr,
                                CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&pDlg)))) {
        return result;
    }
    COMDLG_FILTERSPEC filter[] = {
        { L"Dynamic Link Libraries (*.dll)", L"*.dll" },
        { L"All Files (*.*)",                L"*.*"   }
    };
    pDlg->SetFileTypes(2, filter);
    pDlg->SetDefaultExtension(L"dll");
    pDlg->SetTitle(L"Select DLL to Map");
    if (SUCCEEDED(pDlg->Show(owner))) {
        IShellItem* pItem = nullptr;
        if (SUCCEEDED(pDlg->GetResult(&pItem))) {
            PWSTR pszPath = nullptr;
            if (SUCCEEDED(pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszPath))) {
                int size = WideCharToMultiByte(CP_UTF8, 0, pszPath, -1,
                                               nullptr, 0, nullptr, nullptr);
                result.resize(static_cast<size_t>(size - 1));
                WideCharToMultiByte(CP_UTF8, 0, pszPath, -1,
                                    result.data(), size, nullptr, nullptr);
                CoTaskMemFree(pszPath);
            }
            pItem->Release();
        }
    }
    pDlg->Release();
    return result;
}

void DrawManualMapperPanel(App& app) {
    ImGui::SetNextWindowSize(ImVec2(540, 580), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Manual Mapper", &app.showManualMapper)) {
        ImGui::End();
        return;
    }
    if (!app.processAttached) {
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "No process attached.");
        ImGui::TextWrapped("Attach to a target process first using the Process Selector panel.");
        ImGui::End();
        return;
    }
    ImGui::Text("DLL Path");
    ImGui::Spacing();
    static char s_pathBuf[512] = {};
    {
        static std::string s_lastPath;
        if (s_lastPath != app.mapperDllPath) {
            strncpy(s_pathBuf, app.mapperDllPath.c_str(), sizeof(s_pathBuf) - 1);
            s_pathBuf[sizeof(s_pathBuf) - 1] = 0;
            s_lastPath = app.mapperDllPath;
        }
    }
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 90.0f);
    if (ImGui::InputTextWithHint("##dllpathbuf", "Path to DLL file...",
                                 s_pathBuf, sizeof(s_pathBuf))) {
        app.mapperDllPath = s_pathBuf;
    }
    ImGui::SameLine();
    if (ImGui::Button("Browse...", ImVec2(82, 0))) {
        HWND hwnd = FindWindowA("MemForgeWindow", nullptr);
        if (!hwnd) hwnd = GetForegroundWindow();
        std::string chosen = BrowseForDll(hwnd);
        if (!chosen.empty()) {
            app.mapperDllPath = chosen;
            strncpy(s_pathBuf, chosen.c_str(), sizeof(s_pathBuf) - 1);
            s_pathBuf[sizeof(s_pathBuf) - 1] = 0;
        }
    }
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::Text("Injection Options");
    ImGui::Spacing();
    static ManualMapper::MapConfig s_config;
    ImGui::Checkbox("Erase PE headers after mapping", &s_config.erasePEHeaders);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Zeroes out the MZ/PE header in the remote process after mapping.\n"
            "Makes the loaded image harder to identify by anti-cheat scans.");
    }
    ImGui::Checkbox("Randomize section names", &s_config.randomizeSectionNames);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Overwrites the section name fields in remote memory with random\n"
            "alphanumeric strings, obfuscating the module layout.");
    }
    ImGui::Checkbox("Execute TLS callbacks", &s_config.callTlsCallbacks);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Calls any Thread Local Storage (TLS) callbacks defined in the DLL\n"
            "before the main entry point. Required by some DLLs.");
    }
    ImGui::Checkbox("Call DllMain (DLL_PROCESS_ATTACH)", &s_config.callDllMain);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Invokes the DLL entry point with DLL_PROCESS_ATTACH after mapping\n"
            "is complete. Disable only if the DLL initializes via another mechanism.");
    }
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    bool canMap   = app.processAttached && !app.mapperDllPath.empty();
    bool canUnmap = app.mappedBase != 0;
    if (!canMap) ImGui::BeginDisabled();
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.22f, 0.42f, 0.22f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.28f, 0.55f, 0.28f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.18f, 0.35f, 0.18f, 1.0f));
    if (ImGui::Button("Map DLL", ImVec2(120, 32))) {
        app.mapperLog.clear();
        app.mapperSuccess = false;
        app.mapperError.clear();
        ManualMapper::MapResult result =
            app.manualMapper.Map(app.targetProcess, app.mapperDllPath, s_config);
        app.mapperSuccess = result.success;
        app.mapperLog     = result.log;
        if (result.success) {
            app.mappedBase  = result.remoteBase;
            app.mapperError.clear();
        } else {
            app.mappedBase  = 0;
            app.mapperError = result.errorMessage;
        }
    }
    ImGui::PopStyleColor(3);
    if (!canMap) ImGui::EndDisabled();
    ImGui::SameLine();
    if (!canUnmap) ImGui::BeginDisabled();
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.45f, 0.18f, 0.18f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.60f, 0.22f, 0.22f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.35f, 0.14f, 0.14f, 1.0f));
    if (ImGui::Button("Unmap", ImVec2(90, 32))) {
        bool ok = app.manualMapper.Unmap(app.targetProcess, app.mappedBase);
        if (ok) {
            char hexBuf[32];
            snprintf(hexBuf, sizeof(hexBuf), "0x%llX", (unsigned long long)app.mappedBase);
            app.mapperLog.push_back(std::string("[+] Unmapped region at ") + hexBuf);
            app.mappedBase    = 0;
            app.mapperSuccess = false;
        } else {
            app.mapperLog.push_back("[ERR] Unmap failed (VirtualFreeEx error)");
        }
    }
    ImGui::PopStyleColor(3);
    if (!canUnmap) ImGui::EndDisabled();
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::Text("Status");
    ImGui::Spacing();
    if (app.mappedBase != 0 && app.mapperSuccess) {
        char addrBuf[64];
        snprintf(addrBuf, sizeof(addrBuf), "Mapped at 0x%llX",
                 (unsigned long long)app.mappedBase);
        ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.3f, 1.0f), "%s", addrBuf);
    } else if (!app.mapperError.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f),
                          "Error: %s", app.mapperError.c_str());
    } else {
        ImGui::TextDisabled("Not mapped");
    }
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::Text("Log");
    ImGui::Spacing();
    float logHeight = ImGui::GetContentRegionAvail().y - 8.0f;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.08f, 0.08f, 0.10f, 1.0f));
    if (ImGui::BeginChild("##mapperlog", ImVec2(0, logHeight),
                          true, ImGuiWindowFlags_HorizontalScrollbar)) {
        for (const auto& line : app.mapperLog) {
            if (line.size() >= 5 && line.substr(0, 5) == "[ERR]") {
                ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "%s", line.c_str());
            } else if (line.size() >= 3 && line.substr(0, 3) == "[+]") {
                ImGui::TextColored(ImVec4(0.3f, 0.85f, 0.4f, 1.0f), "%s", line.c_str());
            } else if (line.size() >= 3 && line.substr(0, 3) == "[W]") {
                ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.2f, 1.0f), "%s", line.c_str());
            } else {
                ImGui::TextUnformatted(line.c_str());
            }
        }
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
            ImGui::SetScrollHereY(1.0f);
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::End();
}

} // namespace memforge
