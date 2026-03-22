#pragma once
#include <Windows.h>
#include <TlHelp32.h>
#include <Psapi.h>
#include <string>
#include <vector>
#include <optional>
#include <algorithm>

namespace memforge {

struct ProcessInfo {
    DWORD pid;
    std::string name;
    std::string windowTitle;
    std::string exePath;
    SIZE_T memoryUsage; // in bytes
    // Issue 15: Precomputed lowercase fields for efficient per-frame filtering
    std::string nameLower;
    std::string titleLower;
};

struct ModuleInfo {
    std::string name;
    uintptr_t baseAddress;
    DWORD size;
};

class ProcessManager {
public:
    // Get list of all running processes
    static std::vector<ProcessInfo> EnumerateProcesses();

    // Get modules loaded in a process
    static std::vector<ModuleInfo> GetModules(DWORD pid);

    // Open a process with required permissions for memory operations
    static HANDLE OpenTargetProcess(DWORD pid);

    // Find a process by name (case-insensitive partial match)
    static std::vector<ProcessInfo> FindProcessByName(const std::string& name);

    // Get window title for a process
    static std::string GetProcessWindowTitle(DWORD pid);

    // Check if process is still running
    static bool IsProcessRunning(HANDLE hProcess);

    // Check if we're running as administrator
    static bool IsElevated();

private:
    static std::string WideToUtf8(const std::wstring& wide);
    static std::wstring Utf8ToWide(const std::string& utf8);
};

} // namespace memforge
