#pragma once
#include <Windows.h>
#include <cstdint>
#include <string>
#include <vector>

namespace memforge {

// ─── NT Structures (manually defined) ────────────────────

struct UNICODE_STRING_NT {
    USHORT Length;
    USHORT MaximumLength;
    PWSTR  Buffer;
};

struct PEB_LDR_DATA_NT {
    ULONG Length;
    BOOLEAN Initialized;
    PVOID SsHandle;
    LIST_ENTRY InLoadOrderModuleList;
    LIST_ENTRY InMemoryOrderModuleList;
    LIST_ENTRY InInitializationOrderModuleList;
};

struct LDR_DATA_TABLE_ENTRY_NT {
    LIST_ENTRY InLoadOrderLinks;
    LIST_ENTRY InMemoryOrderLinks;
    LIST_ENTRY InInitializationOrderLinks;
    PVOID DllBase;
    PVOID EntryPoint;
    ULONG SizeOfImage;
    UNICODE_STRING_NT FullDllName;
    UNICODE_STRING_NT BaseDllName;
};

// NT function typedefs
using NtSetInformationThread_t = LONG(NTAPI*)(HANDLE, ULONG, PVOID, ULONG);
using NtSetInformationObject_t = LONG(NTAPI*)(HANDLE, ULONG, PVOID, ULONG);
using NtQueryInformationProcess_t = LONG(NTAPI*)(HANDLE, ULONG, PVOID, ULONG, PULONG);

// Syscall stub typedefs
using NtReadVirtualMemory_t = LONG(NTAPI*)(HANDLE, PVOID, PVOID, SIZE_T, PSIZE_T);
using NtWriteVirtualMemory_t = LONG(NTAPI*)(HANDLE, PVOID, PVOID, SIZE_T, PSIZE_T);
using NtProtectVirtualMemory_t = LONG(NTAPI*)(HANDLE, PVOID*, PSIZE_T, ULONG, PULONG);

// ─── Status ──────────────────────────────────────────────

struct AacStatus {
    bool manualMapAvailable = false;
    bool syscallStubsActive = false;
    bool handleHidden = false;
    bool pebUnlinked = false;
    bool threadHidden = false;
    std::string lastError;
};

// ─── Anti-Anti-Cheat Class ──────────────────────────────

class AntiAntiCheat {
public:
    // Manual map a DLL into target process WITHOUT using LoadLibrary
    static bool ManualMapDll(HANDLE hProcess, const std::string& dllPath);

    // Use direct syscalls instead of going through ntdll
    static bool UseSyscallStubs();

    // Hide our process handle from the target's handle enumeration
    static bool HideHandle(HANDLE hTarget);

    // Unlink our module from the PEB's module list
    static bool UnlinkFromPEB();

    // Hide a thread from debugger enumeration
    static bool HideThread(HANDLE hThread);

    // Get current status
    static AacStatus& GetStatus();

private:
    // Parse ntdll on disk to find syscall number for a given export
    static DWORD GetSyscallNumber(const char* functionName);

    static AacStatus s_status;
};

} // namespace memforge
