// This file is compiled as part of the main GUI app (not the DLL).
// It handles DLL injection and shared memory communication.

#include "speedhack/speedhack.h"
#include <cstdio>
#include <filesystem>

namespace memforge {

SpeedHackController::SpeedHackController() {
    m_dllPath = GetDllPath();
}

SpeedHackController::~SpeedHackController() {
    if (m_injected) {
        Eject();
    }
    CloseSharedMemory();
}

std::string SpeedHackController::GetDllPath() {
    // The speedhack DLL should be next to the main exe
    char exePath[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, exePath, MAX_PATH);

    std::filesystem::path p(exePath);
    p = p.parent_path() / "memforge_speedhack.dll";
    return p.string();
}

bool SpeedHackController::CreateSharedMemory(DWORD pid) {
    char name[256];
    snprintf(name, sizeof(name), SPEEDHACK_SHARED_MEM_NAME, pid);

    m_hSharedMem = CreateFileMappingA(
        INVALID_HANDLE_VALUE, nullptr,
        PAGE_READWRITE, 0, sizeof(SpeedHackSharedData), name
    );

    if (!m_hSharedMem) return false;

    m_sharedData = reinterpret_cast<SpeedHackSharedData*>(
        MapViewOfFile(m_hSharedMem, FILE_MAP_ALL_ACCESS,
                      0, 0, sizeof(SpeedHackSharedData))
    );

    if (!m_sharedData) {
        CloseHandle(m_hSharedMem);
        m_hSharedMem = nullptr;
        return false;
    }

    // Initialize
    m_sharedData->speedMultiplier = 1.0f;
    m_sharedData->enabled = false;

    return true;
}

void SpeedHackController::CloseSharedMemory() {
    if (m_sharedData) {
        UnmapViewOfFile(m_sharedData);
        m_sharedData = nullptr;
    }
    if (m_hSharedMem) {
        CloseHandle(m_hSharedMem);
        m_hSharedMem = nullptr;
    }
}

bool SpeedHackController::Inject(HANDLE hProcess, DWORD pid) {
    if (m_injected) return true;

    m_hProcess = hProcess;
    m_targetPid = pid;

    // Check DLL exists
    if (!std::filesystem::exists(m_dllPath)) {
        return false;
    }

    // Create shared memory BEFORE injecting
    if (!CreateSharedMemory(pid)) {
        return false;
    }

    // ─── Classic DLL injection via CreateRemoteThread + LoadLibrary ───

    // Allocate memory in target for DLL path string
    size_t pathLen = m_dllPath.size() + 1;
    void* remotePath = VirtualAllocEx(
        hProcess, nullptr, pathLen,
        MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE
    );
    if (!remotePath) {
        CloseSharedMemory();
        return false;
    }

    // Write DLL path to target process
    SIZE_T written = 0;
    if (!WriteProcessMemory(hProcess, remotePath, m_dllPath.c_str(), pathLen, &written)) {
        VirtualFreeEx(hProcess, remotePath, 0, MEM_RELEASE);
        CloseSharedMemory();
        return false;
    }

    // Get LoadLibraryA address (it's the same in all processes)
    FARPROC loadLibAddr = GetProcAddress(GetModuleHandleA("kernel32.dll"), "LoadLibraryA");
    if (!loadLibAddr) {
        VirtualFreeEx(hProcess, remotePath, 0, MEM_RELEASE);
        CloseSharedMemory();
        return false;
    }

    // Create remote thread to call LoadLibraryA(dllPath)
    HANDLE hThread = CreateRemoteThread(
        hProcess, nullptr, 0,
        reinterpret_cast<LPTHREAD_START_ROUTINE>(loadLibAddr),
        remotePath, 0, nullptr
    );

    if (!hThread) {
        VirtualFreeEx(hProcess, remotePath, 0, MEM_RELEASE);
        CloseSharedMemory();
        return false;
    }

    // Wait for LoadLibrary to complete
    WaitForSingleObject(hThread, 5000);

    // Get the module handle (return value of LoadLibrary)
    DWORD exitCode = 0;
    GetExitCodeThread(hThread, &exitCode);
    m_remoteDllBase = reinterpret_cast<void*>(static_cast<uintptr_t>(exitCode));

    CloseHandle(hThread);
    VirtualFreeEx(hProcess, remotePath, 0, MEM_RELEASE);

    // On x64, the HMODULE might be truncated in exitCode (32-bit).
    // As long as it's non-zero, the DLL loaded successfully.
    m_injected = (exitCode != 0);

    if (!m_injected) {
        CloseSharedMemory();
    }

    return m_injected;
}

bool SpeedHackController::Eject() {
    if (!m_injected || !m_hProcess) return false;

    // We could call FreeLibrary remotely, but it's safer to just
    // disable the hooks and leave the DLL loaded. When the target
    // process exits, it'll be cleaned up automatically.

    if (m_sharedData) {
        m_sharedData->enabled = false;
    }

    m_injected = false;
    CloseSharedMemory();
    return true;
}

void SpeedHackController::SetSpeed(float multiplier) {
    if (m_sharedData) {
        m_sharedData->speedMultiplier = multiplier;
    }
}

void SpeedHackController::SetEnabled(bool enabled) {
    if (m_sharedData) {
        m_sharedData->enabled = enabled;
    }
}

float SpeedHackController::GetSpeed() const {
    if (m_sharedData) return m_sharedData->speedMultiplier;
    return 1.0f;
}

bool SpeedHackController::IsEnabled() const {
    if (m_sharedData) return m_sharedData->enabled;
    return false;
}

} // namespace memforge
