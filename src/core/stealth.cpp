#include "core/stealth.h"
#include "core/process_manager.h"
#include <TlHelp32.h>
#include <dwmapi.h>
#include <filesystem>
#include <algorithm>
#include <chrono>
#include <cstring>

namespace memforge {

// ═══════════════════════════════════════════════════════════
//  Name Generator
// ═══════════════════════════════════════════════════════════

std::mt19937& NameGenerator::GetRNG() {
    static std::mt19937 rng(static_cast<unsigned int>(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    return rng;
}

std::string NameGenerator::RandomString(int minLen, int maxLen) {
    static const char charset[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    std::uniform_int_distribution<int> lenDist(minLen, maxLen);
    std::uniform_int_distribution<int> charDist(0, sizeof(charset) - 2);

    auto& rng = GetRNG();
    int len = lenDist(rng);
    std::string result;
    result.reserve(len);
    for (int i = 0; i < len; i++) {
        result += charset[charDist(rng)];
    }
    return result;
}

std::string NameGenerator::GetDecoyName() {
    static const std::vector<std::string> decoyNames = {
        "Windows Shell Infrastructure Host",
        "Runtime Broker",
        "Microsoft Text Input Application",
        "Windows Security Health Service",
        "Application Frame Host",
        "System Settings",
        "Windows Update Assistant",
        "Microsoft Edge Update",
        "Desktop Window Manager",
        "Windows Audio Device Graph",
        "Connected Devices Platform",
        "Delivery Optimization",
        "Background Intelligent Transfer",
        "Windows Modules Installer",
        "Security Health Systray",
        "Microsoft Windows Search",
        "Windows Push Notifications",
        "Credential Manager UI Host",
        "Windows Biometric Service",
        "Clipboard User Service",
        "Data Usage Monitor",
        "Device Association Framework",
        "Network Connection Broker",
        "Storage Service",
        "User Data Access",
        "Windows Event Collector",
        "Print Spooler Host",
        "Diagnostic Policy Service",
        "Windows Font Cache Service",
        "Superfetch Service Host"
    };

    std::uniform_int_distribution<size_t> dist(0, decoyNames.size() - 1);
    return decoyNames[dist(GetRNG())];
}

std::string NameGenerator::GenerateWindowTitle() {
    return GetDecoyName();
}

std::string NameGenerator::GenerateClassName() {
    static const std::vector<std::string> prefixes = {
        "Windows.UI.", "Microsoft.", "Shell.", "Win32.",
        "Afx:", "HwndWrapper[", "Chrome_Widget"
    };
    static const std::vector<std::string> suffixes = {
        "Host", "Container", "Window", "Frame",
        "View", "Panel", "Surface"
    };

    auto& rng = GetRNG();
    std::uniform_int_distribution<size_t> pDist(0, prefixes.size() - 1);
    std::uniform_int_distribution<size_t> sDist(0, suffixes.size() - 1);

    return prefixes[pDist(rng)] + suffixes[sDist(rng)] +
           "." + RandomString(4, 8);
}

std::string NameGenerator::GenerateExeName() {
    static const std::vector<std::string> exeNames = {
        "svchost", "RuntimeBroker", "SearchHost",
        "ShellExperienceHost", "SystemSettings",
        "TextInputHost", "SecurityHealthService",
        "WmiPrvSE", "dllhost", "sihost",
        "taskhostw", "ctfmon", "fontdrvhost",
        "dwm", "conhost", "smartscreen",
        "CompatTelRunner", "MsMpEng", "NisSrv",
        "audiodg", "spoolsv", "lsass",
        "wlanext", "dashost", "DeviceCensus"
    };

    auto& rng = GetRNG();
    std::uniform_int_distribution<size_t> dist(0, exeNames.size() - 1);
    return exeNames[dist(rng)];
}

// ═══════════════════════════════════════════════════════════
//  Stealth Manager
// ═══════════════════════════════════════════════════════════

StealthManager::StealthManager() {}

StealthManager::~StealthManager() {
    if (m_active) Restore();
}

void StealthManager::Apply(HWND hwnd, const StealthConfig& config) {
    m_hwnd = hwnd;
    m_config = config;

    // Save original state
    char title[512] = {};
    GetWindowTextA(hwnd, title, sizeof(title));
    m_originalTitle = title;
    m_originalExStyle = GetWindowLongPtrA(hwnd, GWL_EXSTYLE);

    if (config.randomizeWindowTitle)
        RandomizeWindowTitle(hwnd, config.customWindowTitle);
    if (config.hiddenFromTaskbar)
        HideFromTaskbar(hwnd, true);
    if (config.mutexCloaking)
        CleanupMutexes();
    if (config.clearPeHeaders)
        ClearPEHeaders();
    if (config.blockWindowEnumeration)
        BlockWindowEnumeration(hwnd, true);

    m_active = true;
}

void StealthManager::Restore() {
    if (!m_hwnd) return;

    if (!m_originalTitle.empty())
        SetWindowTextA(m_hwnd, m_originalTitle.c_str());

    if (m_config.hiddenFromTaskbar)
        HideFromTaskbar(m_hwnd, false);

    if (m_config.blockWindowEnumeration)
        BlockWindowEnumeration(m_hwnd, false);

    // Restore PE headers
    if (m_config.clearPeHeaders && !m_originalPEHeader.empty()) {
        HMODULE hModule = GetModuleHandleA(nullptr);
        DWORD oldProtect;
        if (VirtualProtect(hModule, m_originalPEHeader.size(),
                           PAGE_READWRITE, &oldProtect)) {
            memcpy(hModule, m_originalPEHeader.data(), m_originalPEHeader.size());
            VirtualProtect(hModule, m_originalPEHeader.size(),
                          oldProtect, &oldProtect);
        }
    }

    m_active = false;
}

void StealthManager::RandomizeWindowTitle(HWND hwnd, const std::string& customTitle) {
    std::string newTitle = customTitle.empty()
        ? NameGenerator::GenerateWindowTitle()
        : customTitle;
    SetWindowTextA(hwnd, newTitle.c_str());
    m_currentTitle = newTitle;
}

void StealthManager::HideFromTaskbar(HWND hwnd, bool hide) {
    if (hide) {
        LONG_PTR style = GetWindowLongPtrA(hwnd, GWL_EXSTYLE);
        style |= WS_EX_TOOLWINDOW;
        style &= ~WS_EX_APPWINDOW;
        SetWindowLongPtrA(hwnd, GWL_EXSTYLE, style);
        ShowWindow(hwnd, SW_HIDE);
        ShowWindow(hwnd, SW_SHOW);
    } else {
        SetWindowLongPtrA(hwnd, GWL_EXSTYLE, m_originalExStyle);
        ShowWindow(hwnd, SW_HIDE);
        ShowWindow(hwnd, SW_SHOW);
    }
}

void StealthManager::ClearPEHeaders() {
    HMODULE hModule = GetModuleHandleA(nullptr);
    if (!hModule) return;

    auto dosHeader = reinterpret_cast<PIMAGE_DOS_HEADER>(hModule);
    auto ntHeaders = reinterpret_cast<PIMAGE_NT_HEADERS>(
        reinterpret_cast<uint8_t*>(hModule) + dosHeader->e_lfanew);

    DWORD headerSize = ntHeaders->OptionalHeader.SizeOfHeaders;

    // Save original for restoration
    m_originalPEHeader.resize(headerSize);
    memcpy(m_originalPEHeader.data(), hModule, headerSize);

    // Zero out headers (keep "MZ" magic so module handle stays valid)
    DWORD oldProtect;
    if (VirtualProtect(hModule, headerSize, PAGE_READWRITE, &oldProtect)) {
        memset(reinterpret_cast<uint8_t*>(hModule) + 2, 0, headerSize - 2);
        VirtualProtect(hModule, headerSize, oldProtect, &oldProtect);
    }
}

void StealthManager::CleanupMutexes() {
    // Close any mutex with identifiable names
    HANDLE hMutex = OpenMutexA(MUTEX_ALL_ACCESS, FALSE, "MemForge_Active");
    if (hMutex) CloseHandle(hMutex);
}

void StealthManager::BlockWindowEnumeration(HWND hwnd, bool block) {
    BOOL cloaked = block ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd, 14 /* DWMWA_CLOAK */, &cloaked, sizeof(cloaked));
}

// ─── Self-Relaunch ───────────────────────────────────────

bool StealthManager::IsRunningRandomized() {
    char exePath[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    std::filesystem::path p(exePath);
    std::string name = p.stem().string();
    std::string nameLower = name;
    std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::tolower);
    return nameLower != "memforge";
}

bool StealthManager::RelaunchWithRandomName(int argc, char** argv) {
    if (IsRunningRandomized()) return false;

    char currentPath[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, currentPath, MAX_PATH);

    std::string newName = NameGenerator::GenerateExeName();
    std::filesystem::path tempDir = std::filesystem::temp_directory_path();
    std::filesystem::path newPath = tempDir / (newName + ".exe");

    try {
        std::filesystem::copy_file(currentPath, newPath,
                                    std::filesystem::copy_options::overwrite_existing);
    } catch (...) {
        return false;
    }

    // Also copy the speedhack DLL with a renamed version
    std::filesystem::path currentDir = std::filesystem::path(currentPath).parent_path();
    std::filesystem::path speedhackSrc = currentDir / "memforge_speedhack.dll";
    if (std::filesystem::exists(speedhackSrc)) {
        std::string dllName = newName + "_module.dll";
        std::filesystem::path speedhackDst = tempDir / dllName;
        try {
            std::filesystem::copy_file(speedhackSrc, speedhackDst,
                                        std::filesystem::copy_options::overwrite_existing);
        } catch (...) {}
    }

    // Launch the renamed copy
    STARTUPINFOA si = {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};

    std::string cmdLine = "\"" + newPath.string() + "\" --stealth";
    for (int i = 1; i < argc; i++) {
        cmdLine += " ";
        cmdLine += argv[i];
    }

    if (CreateProcessA(newPath.string().c_str(),
                        const_cast<char*>(cmdLine.c_str()),
                        nullptr, nullptr, FALSE, 0, nullptr, nullptr,
                        &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return true; // Caller should exit
    }

    return false;
}

// ─── Detection Check ─────────────────────────────────────

StealthManager::DetectionStatus StealthManager::CheckForDetection() {
    DetectionStatus status;

    status.debuggerAttached = IsDebuggerPresent() != FALSE;

    static const std::vector<std::string> knownTools = {
        "ollydbg.exe", "x64dbg.exe", "x32dbg.exe",
        "ida.exe", "ida64.exe", "idag.exe", "idag64.exe",
        "idaw.exe", "idaw64.exe",
        "windbg.exe", "dbgview.exe",
        "processhacker.exe", "procmon.exe", "procexp.exe",
        "wireshark.exe", "fiddler.exe",
        "httpdebugger.exe", "httpdebuggerpro.exe",
        "ghidra.exe", "ghidrarun.exe",
        "dnspy.exe", "de4dot.exe",
        "EasyAntiCheat.exe", "EasyAntiCheat_EOS.exe",
        "BEService.exe", "BEDaisy.sys",
        "vgc.exe", "vgtray.exe"
    };

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32A pe = {};
        pe.dwSize = sizeof(pe);

        if (Process32FirstA(snapshot, &pe)) {
            do {
                std::string procName = pe.szExeFile;
                std::string procLower = procName;
                std::transform(procLower.begin(), procLower.end(),
                             procLower.begin(), ::tolower);

                for (auto& tool : knownTools) {
                    std::string toolLower = tool;
                    std::transform(toolLower.begin(), toolLower.end(),
                                 toolLower.begin(), ::tolower);
                    if (procLower == toolLower) {
                        status.analysisToolsRunning = true;
                        status.detectedTools.push_back(procName);
                    }
                }
            } while (Process32NextA(snapshot, &pe));
        }
        CloseHandle(snapshot);
    }

    return status;
}

} // namespace memforge
