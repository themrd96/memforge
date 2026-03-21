#include "core/process_manager.h"
#include <sstream>

namespace memforge {

// ─── Helpers ───────────────────────────────────────────────

std::string ProcessManager::WideToUtf8(const std::wstring& wide) {
    if (wide.empty()) return {};
    int size = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), (int)wide.size(),
                                   nullptr, 0, nullptr, nullptr);
    std::string result(size, 0);
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), (int)wide.size(),
                        result.data(), size, nullptr, nullptr);
    return result;
}

std::wstring ProcessManager::Utf8ToWide(const std::string& utf8) {
    if (utf8.empty()) return {};
    int size = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(),
                                   nullptr, 0);
    std::wstring result(size, 0);
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(),
                        result.data(), size);
    return result;
}

// ─── Enumerate all processes ──────────────────────────────

std::vector<ProcessInfo> ProcessManager::EnumerateProcesses() {
    std::vector<ProcessInfo> result;

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return result;

    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);

    if (Process32FirstW(snapshot, &pe)) {
        do {
            ProcessInfo info;
            info.pid = pe.th32ProcessID;
            info.name = WideToUtf8(pe.szExeFile);
            info.memoryUsage = 0;

            // Try to get more info by opening the process
            HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ,
                                       FALSE, pe.th32ProcessID);
            if (hProc) {
                // Get exe path
                wchar_t path[MAX_PATH] = {};
                DWORD pathSize = MAX_PATH;
                if (QueryFullProcessImageNameW(hProc, 0, path, &pathSize)) {
                    info.exePath = WideToUtf8(path);
                }

                // Get memory usage
                PROCESS_MEMORY_COUNTERS pmc{};
                if (GetProcessMemoryInfo(hProc, &pmc, sizeof(pmc))) {
                    info.memoryUsage = pmc.WorkingSetSize;
                }

                CloseHandle(hProc);
            }

            info.windowTitle = GetProcessWindowTitle(pe.th32ProcessID);
            result.push_back(std::move(info));
        } while (Process32NextW(snapshot, &pe));
    }

    CloseHandle(snapshot);

    // Sort by name
    std::sort(result.begin(), result.end(),
              [](const ProcessInfo& a, const ProcessInfo& b) {
                  std::string aLow = a.name, bLow = b.name;
                  std::transform(aLow.begin(), aLow.end(), aLow.begin(), ::tolower);
                  std::transform(bLow.begin(), bLow.end(), bLow.begin(), ::tolower);
                  return aLow < bLow;
              });

    return result;
}

// ─── Get modules ─────────────────────────────────────────

std::vector<ModuleInfo> ProcessManager::GetModules(DWORD pid) {
    std::vector<ModuleInfo> result;

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snapshot == INVALID_HANDLE_VALUE) return result;

    MODULEENTRY32W me{};
    me.dwSize = sizeof(me);

    if (Module32FirstW(snapshot, &me)) {
        do {
            ModuleInfo mod;
            mod.name = WideToUtf8(me.szModule);
            mod.baseAddress = reinterpret_cast<uintptr_t>(me.modBaseAddr);
            mod.size = me.modBaseSize;
            result.push_back(std::move(mod));
        } while (Module32NextW(snapshot, &me));
    }

    CloseHandle(snapshot);
    return result;
}

// ─── Open process for memory operations ──────────────────

HANDLE ProcessManager::OpenTargetProcess(DWORD pid) {
    return OpenProcess(
        PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_VM_OPERATION |
        PROCESS_QUERY_INFORMATION | PROCESS_CREATE_THREAD,
        FALSE, pid
    );
}

// ─── Find process by name ────────────────────────────────

std::vector<ProcessInfo> ProcessManager::FindProcessByName(const std::string& name) {
    auto all = EnumerateProcesses();
    std::vector<ProcessInfo> filtered;

    std::string nameLower = name;
    std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::tolower);

    for (auto& p : all) {
        std::string pLower = p.name;
        std::transform(pLower.begin(), pLower.end(), pLower.begin(), ::tolower);
        if (pLower.find(nameLower) != std::string::npos) {
            filtered.push_back(std::move(p));
        }
    }

    return filtered;
}

// ─── Window title ─────────────────────────────────────────

struct EnumWindowData {
    DWORD pid;
    std::string title;
};

static BOOL CALLBACK EnumWindowProc(HWND hwnd, LPARAM lParam) {
    auto* data = reinterpret_cast<EnumWindowData*>(lParam);
    DWORD windowPid = 0;
    GetWindowThreadProcessId(hwnd, &windowPid);

    if (windowPid == data->pid && IsWindowVisible(hwnd)) {
        wchar_t title[512] = {};
        GetWindowTextW(hwnd, title, 512);
        if (wcslen(title) > 0) {
            int size = WideCharToMultiByte(CP_UTF8, 0, title, -1, nullptr, 0, nullptr, nullptr);
            data->title.resize(size - 1);
            WideCharToMultiByte(CP_UTF8, 0, title, -1, data->title.data(), size, nullptr, nullptr);
            return FALSE; // stop enumerating
        }
    }
    return TRUE;
}

std::string ProcessManager::GetProcessWindowTitle(DWORD pid) {
    EnumWindowData data{pid, {}};
    EnumWindows(EnumWindowProc, reinterpret_cast<LPARAM>(&data));
    return data.title;
}

// ─── Process running check ────────────────────────────────

bool ProcessManager::IsProcessRunning(HANDLE hProcess) {
    DWORD exitCode = 0;
    if (!GetExitCodeProcess(hProcess, &exitCode)) return false;
    return exitCode == STILL_ACTIVE;
}

// ─── Elevation check ──────────────────────────────────────

bool ProcessManager::IsElevated() {
    BOOL isElevated = FALSE;
    HANDLE token = nullptr;

    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        TOKEN_ELEVATION elevation{};
        DWORD size = sizeof(elevation);
        if (GetTokenInformation(token, TokenElevation, &elevation,
                                sizeof(elevation), &size)) {
            isElevated = elevation.TokenIsElevated;
        }
        CloseHandle(token);
    }

    return isElevated != FALSE;
}

} // namespace memforge
