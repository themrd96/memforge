#pragma once
#include <Windows.h>
#include <TlHelp32.h>
#include <Psapi.h>
#include <AclAPI.h>
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

// How OpenTargetProcess managed to obtain a handle
enum class AttachMethod {
    Normal,        // Standard OpenProcess - no protection detected
    DirectSyscall, // ntdll hook detected and bypassed via raw syscall
    DaclBypass,    // DACL-based protection bypassed via WRITE_DAC + NULL DACL
    Failed         // All methods exhausted
};

struct OpenProcessResult {
    HANDLE      handle = nullptr;
    AttachMethod method = AttachMethod::Failed;
    NTSTATUS    lastStatus = 0;

    const char* MethodName() const {
        switch (method) {
            case AttachMethod::Normal:        return "Normal";
            case AttachMethod::DirectSyscall: return "Direct Syscall (ntdll hook bypassed)";
            case AttachMethod::DaclBypass:    return "DACL Bypass (WRITE_DAC + NULL DACL)";
            default:                          return "Failed";
        }
    }

    bool Ok() const { return handle != nullptr && handle != INVALID_HANDLE_VALUE; }
};

class ProcessManager {
public:
    // Get list of all running processes
    static std::vector<ProcessInfo> EnumerateProcesses();

    // Get modules loaded in a process
    static std::vector<ModuleInfo> GetModules(DWORD pid);

    // Open a process with required permissions, escalating through bypass methods
    // Returns a result struct describing the handle and which method succeeded
    static OpenProcessResult OpenTargetProcess(DWORD pid);

    // Find a process by name (case-insensitive partial match)
    static std::vector<ProcessInfo> FindProcessByName(const std::string& name);

    // Get window title for a process
    static std::string GetProcessWindowTitle(DWORD pid);

    // Check if process is still running
    static bool IsProcessRunning(HANDLE hProcess);

    // Check if we're running as administrator
    static bool IsElevated();

private:
    static std::string  WideToUtf8(const std::wstring& wide);
    static std::wstring Utf8ToWide(const std::string& utf8);

    // Read syscall number for NtOpenProcess from ntdll.dll on disk
    // (bypasses in-memory hooks that patch the ntdll stubs)
    static UINT16 ReadNtOpenProcessSyscallNumber();

    // Execute NtOpenProcess via a raw syscall stub, bypassing any ntdll hooks
    static HANDLE DirectSyscallOpenProcess(UINT16 syscallNum, DWORD pid, ACCESS_MASK access);

    // Wipe the process DACL via WRITE_DAC (owner implicit right),
    // then reopen with the full desired access mask
    static HANDLE TryDaclBypass(UINT16 syscallNum, DWORD pid, ACCESS_MASK desiredAccess);
};

} // namespace memforge
