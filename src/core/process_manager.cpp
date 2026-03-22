#include "core/process_manager.h"
#include <sstream>
#include <unordered_map>
#include <winternl.h>

#pragma comment(lib, "advapi32.lib")

// Forward declaration — defined in the direct syscall helpers section below
static bool IsNtOpenProcessHooked();

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

// ─── Issue 14: Build pid->windowTitle map once via a single EnumWindows call ──

struct EnumAllWindowsData {
    std::unordered_map<DWORD, std::string>* pidToTitle;
};

static BOOL CALLBACK EnumAllWindowsProc(HWND hwnd, LPARAM lParam) {
    auto* data = reinterpret_cast<EnumAllWindowsData*>(lParam);
    if (!IsWindowVisible(hwnd)) return TRUE;

    wchar_t title[512] = {};
    GetWindowTextW(hwnd, title, 512);
    if (wcslen(title) == 0) return TRUE;

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == 0) return TRUE;

    // Only record first visible titled window per pid
    if (data->pidToTitle->find(pid) == data->pidToTitle->end()) {
        int size = WideCharToMultiByte(CP_UTF8, 0, title, -1, nullptr, 0, nullptr, nullptr);
        std::string titleUtf8(size - 1, '\0');
        WideCharToMultiByte(CP_UTF8, 0, title, -1, titleUtf8.data(), size, nullptr, nullptr);
        (*data->pidToTitle)[pid] = std::move(titleUtf8);
    }
    return TRUE;
}

// ─── Enumerate all processes ──────────────────────────────

std::vector<ProcessInfo> ProcessManager::EnumerateProcesses() {
    std::vector<ProcessInfo> result;

    // Check once whether ntdll is hooked before the enumeration loop.
    // If hooked, we must NOT call OpenProcess() on any process during enumeration —
    // the hook handler may kill the protected game process as a side effect of any
    // OpenProcess call it intercepts, even for harmless access masks.
    const bool ntdllHooked = IsNtOpenProcessHooked();

    // Issue 14: Build the pid->title map once before the process loop
    std::unordered_map<DWORD, std::string> pidToTitle;
    {
        EnumAllWindowsData data{ &pidToTitle };
        EnumWindows(EnumAllWindowsProc, reinterpret_cast<LPARAM>(&data));
    }

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

            // Only open each process for extra info when ntdll is clean.
            // When hooked, skip this entirely — any OpenProcess call routes
            // through the AC handler which may kill the protected process.
            if (!ntdllHooked) {
                HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,
                                           FALSE, pe.th32ProcessID);
                if (hProc) {
                    wchar_t path[MAX_PATH] = {};
                    DWORD pathSize = MAX_PATH;
                    if (QueryFullProcessImageNameW(hProc, 0, path, &pathSize))
                        info.exePath = WideToUtf8(path);

                    PROCESS_MEMORY_COUNTERS pmc{};
                    if (GetProcessMemoryInfo(hProc, &pmc, sizeof(pmc)))
                        info.memoryUsage = pmc.WorkingSetSize;

                    CloseHandle(hProc);
                }
            }

            // Issue 14: Look up title from the prebuilt map instead of calling EnumWindows per process
            auto it = pidToTitle.find(pe.th32ProcessID);
            if (it != pidToTitle.end()) {
                info.windowTitle = it->second;
            }

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

// ─── Direct syscall helpers ───────────────────────────────

// Check whether NtOpenProcess in the in-memory ntdll has been inline-hooked.
// A hooked stub starts with 0xE9 (JMP rel32) instead of 0x4C (mov r10,rcx).
// If hooked, calling OpenProcess() would trigger the AC handler — so we must
// skip the normal call entirely and go straight to the direct syscall path.
static bool IsNtOpenProcessHooked() {
    HMODULE hNtdll = GetModuleHandleW(L"ntdll.dll");
    if (!hNtdll) return false;
    auto* fn = reinterpret_cast<BYTE*>(GetProcAddress(hNtdll, "NtOpenProcess"));
    if (!fn)  return false;
    return fn[0] == 0xE9; // JMP = inline hook present
}

// Read the real syscall number for NtOpenProcess from ntdll.dll on disk.
// This is necessary because some anti-cheats patch the in-memory ntdll stub
// with a JMP to their own handler (inline hook), making the stub useless.
// Reading from disk gives us the unhooked bytes and the real syscall index.
UINT16 ProcessManager::ReadNtOpenProcessSyscallNumber() {
    HANDLE hFile = CreateFileW(L"C:\\Windows\\System32\\ntdll.dll",
        GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return 0xFFFF;

    DWORD fileSize = GetFileSize(hFile, nullptr);
    auto buf = std::make_unique<BYTE[]>(fileSize);
    DWORD bytesRead = 0;
    ReadFile(hFile, buf.get(), fileSize, &bytesRead, nullptr);
    CloseHandle(hFile);

    if (bytesRead != fileSize) return 0xFFFF;

    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(buf.get());
    auto* nt  = reinterpret_cast<IMAGE_NT_HEADERS*>(buf.get() + dos->e_lfanew);
    auto* exp = reinterpret_cast<IMAGE_EXPORT_DIRECTORY*>(
        buf.get() + nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress);

    auto* names = reinterpret_cast<DWORD*>(buf.get() + exp->AddressOfNames);
    auto* ords  = reinterpret_cast<WORD*> (buf.get() + exp->AddressOfNameOrdinals);
    auto* funcs = reinterpret_cast<DWORD*>(buf.get() + exp->AddressOfFunctions);

    for (DWORD i = 0; i < exp->NumberOfNames; i++) {
        const char* name = reinterpret_cast<const char*>(buf.get() + names[i]);
        if (strcmp(name, "NtOpenProcess") != 0) continue;

        BYTE* fn = buf.get() + funcs[ords[i]];
        // Unhooked stub starts with: 4C 8B D1 (mov r10, rcx) B8 XX XX 00 00 (mov eax, syscall#)
        if (fn[0] == 0x4C && fn[1] == 0x8B && fn[2] == 0xD1 && fn[3] == 0xB8) {
            return *reinterpret_cast<UINT16*>(fn + 4);
        }
        break; // found but hooked on disk too — give up
    }
    return 0xFFFF;
}

// Execute NtOpenProcess via a hand-rolled syscall stub.
// This jumps directly to the kernel, completely skipping any ntdll hooks.
HANDLE ProcessManager::DirectSyscallOpenProcess(UINT16 syscallNum, DWORD pid, ACCESS_MASK access) {
    if (syscallNum == 0xFFFF) return nullptr;

    // Build: mov r10,rcx / mov eax,N / syscall / ret
    BYTE code[] = { 0x4C,0x8B,0xD1, 0xB8,0x00,0x00,0x00,0x00, 0x0F,0x05, 0xC3 };
    *reinterpret_cast<UINT16*>(code + 4) = syscallNum;

    void* stub = VirtualAlloc(nullptr, sizeof(code),
        MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!stub) return nullptr;
    memcpy(stub, code, sizeof(code));

    using NtOpenProcessFn = NTSTATUS(NTAPI*)(
        PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES, CLIENT_ID*);

    HANDLE hProcess = nullptr;
    OBJECT_ATTRIBUTES oa{};
    oa.Length = sizeof(oa);
    CLIENT_ID cid{ reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(pid)), nullptr };

    reinterpret_cast<NtOpenProcessFn>(stub)(&hProcess, access, &oa, &cid);

    VirtualFree(stub, 0, MEM_RELEASE);
    return hProcess;
}

// DACL bypass: open with WRITE_DAC (an implicit owner right not present in most
// deny masks), wipe the DACL to NULL (grants Everyone full access), then reopen
// with the full desired access mask.
HANDLE ProcessManager::TryDaclBypass(UINT16 syscallNum, DWORD pid, ACCESS_MASK desiredAccess) {
    // Step 1 — get a WRITE_DAC handle (owner implicit right, rarely denied)
    HANDLE hWriteDac = DirectSyscallOpenProcess(syscallNum, pid, WRITE_DAC);
    if (!hWriteDac) return nullptr;

    // Step 2 — replace the DACL with NULL (full access to everyone)
    DWORD res = SetSecurityInfo(hWriteDac, SE_KERNEL_OBJECT,
        DACL_SECURITY_INFORMATION, nullptr, nullptr, nullptr, nullptr);
    CloseHandle(hWriteDac);

    if (res != ERROR_SUCCESS) return nullptr;

    // Step 3 — now open freely with the originally desired access
    return DirectSyscallOpenProcess(syscallNum, pid, desiredAccess);
}

// ─── Open process for memory operations ──────────────────

// Escalating attach: tries progressively more aggressive methods until one works.
//  1. Normal OpenProcess            — works against unprotected processes
//  2. Direct syscall                — bypasses user-mode ntdll inline hooks
//  3. DACL bypass + direct syscall  — bypasses DACL-based lockouts (e.g. NinjaEye)
OpenProcessResult ProcessManager::OpenTargetProcess(DWORD pid) {
    constexpr ACCESS_MASK kDesired =
        PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_VM_OPERATION |
        PROCESS_QUERY_INFORMATION;

    OpenProcessResult result{};

    // Detect the ntdll hook BEFORE attempting any normal OpenProcess call.
    // If the hook is present, calling OpenProcess() would route through the AC
    // handler which may kill the game as a side-effect — not just deny access.
    // Skipping Method 1 when hooked means the AC never sees our attach attempt.
    const bool hooked = IsNtOpenProcessHooked();
    UINT16 sysNum = 0xFFFF;

    // ── Method 1: Normal (only if ntdll is clean) ────────────────────────────
    if (!hooked) {
        result.handle = OpenProcess(kDesired, FALSE, pid);
        if (result.handle) {
            result.method = AttachMethod::Normal;
            return result;
        }
    }

    // ── Method 2: Direct syscall (bypasses ntdll inline hooks) ───────────────
    // Also used as the first attempt when a hook is detected, so the hook
    // handler never fires and cannot trigger a game-kill side effect.
    sysNum = ReadNtOpenProcessSyscallNumber();
    result.handle = DirectSyscallOpenProcess(sysNum, pid, kDesired);
    if (result.handle) {
        result.method = AttachMethod::DirectSyscall;
        return result;
    }

    // ── Method 3: DACL bypass (WRITE_DAC owner right → NULL DACL → reopen) ──
    result.handle = TryDaclBypass(sysNum, pid, kDesired);
    if (result.handle) {
        result.method = AttachMethod::DaclBypass;
        return result;
    }

    result.method     = AttachMethod::Failed;
    result.lastStatus = GetLastError();
    return result;
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

// ─── Window title (kept for external callers) ─────────────

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
