#pragma once
#include <Windows.h>
#include <string>
#include <cstdint>

namespace memforge {

// Shared memory structure for communicating with the injected DLL
// The main app writes speed values here, the DLL reads them
struct SpeedHackSharedData {
    float speedMultiplier;  // 1.0 = normal, 2.0 = 2x speed, 0.5 = half speed
    bool enabled;
    DWORD padding[6];       // keep cache line friendly
};

// Name of the shared memory-mapped file
constexpr const char* SPEEDHACK_SHARED_MEM_NAME = "MemForge_SpeedHack_%lu";

class SpeedHackController {
public:
    SpeedHackController();
    ~SpeedHackController();

    // Inject the speedhack DLL into target process
    bool Inject(HANDLE hProcess, DWORD pid);

    // Remove the DLL from target process
    bool Eject();

    // Set speed multiplier (1.0 = normal)
    void SetSpeed(float multiplier);

    // Enable/disable without ejecting
    void SetEnabled(bool enabled);

    float GetSpeed() const;
    bool IsEnabled() const;
    bool IsInjected() const { return m_injected; }

    // Get the path to the speedhack DLL
    static std::string GetDllPath();

private:
    bool CreateSharedMemory(DWORD pid);
    void CloseSharedMemory();

    HANDLE m_hProcess = nullptr;
    DWORD m_targetPid = 0;
    bool m_injected = false;
    HANDLE m_hSharedMem = nullptr;
    SpeedHackSharedData* m_sharedData = nullptr;
    void* m_remoteDllBase = nullptr;
    std::string m_dllPath;
};

} // namespace memforge
