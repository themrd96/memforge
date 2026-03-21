/*
 * MemForge SpeedHack DLL
 *
 * This DLL is injected into the target process. It hooks Windows timing
 * functions (QueryPerformanceCounter, GetTickCount, GetTickCount64,
 * timeGetTime) and multiplies the returned time delta by a speed factor.
 *
 * Communication with the main MemForge app happens through a named
 * shared memory region containing the speed multiplier.
 *
 * Hook method: IAT (Import Address Table) patching + inline trampolines.
 * For simplicity and reliability, we use MinHook-style manual trampolines.
 */

#include <Windows.h>
#include <Psapi.h>
#include <cstdint>
#include <cstdio>
#include <cstring>

#pragma comment(lib, "psapi.lib")

// ─── Shared data structure ────────────────────────────────

struct SpeedHackSharedData {
    float speedMultiplier;
    bool enabled;
    DWORD padding[6];
};

// ─── Globals ──────────────────────────────────────────────

static SpeedHackSharedData* g_sharedData = nullptr;
static HANDLE g_hSharedMem = nullptr;

// Original function pointers
static BOOL(WINAPI* g_origQPC)(LARGE_INTEGER* lpCounter) = nullptr;
static DWORD(WINAPI* g_origGetTickCount)() = nullptr;
static ULONGLONG(WINAPI* g_origGetTickCount64)() = nullptr;
static DWORD(WINAPI* g_origTimeGetTime)() = nullptr;

// Base values captured at hook install time
static LARGE_INTEGER g_qpcBase = {};
static DWORD g_tickBase = 0;
static ULONGLONG g_tick64Base = 0;
static DWORD g_timeBase = 0;
static bool g_baseCaptured = false;

// Accumulated "virtual" time for smooth speed changes
static double g_virtualQpcOffset = 0.0;
static double g_virtualTickOffset = 0.0;
static double g_virtualTick64Offset = 0.0;
static double g_virtualTimeOffset = 0.0;

static LARGE_INTEGER g_lastRealQpc = {};
static DWORD g_lastRealTick = 0;
static ULONGLONG g_lastRealTick64 = 0;
static DWORD g_lastRealTime = 0;

// ─── Simple IAT hook helper ──────────────────────────────

static bool PatchIAT(HMODULE module, const char* dllName,
                      void* origFunc, void* hookFunc) {
    if (!module) return false;

    auto dosHeader = reinterpret_cast<PIMAGE_DOS_HEADER>(module);
    auto ntHeaders = reinterpret_cast<PIMAGE_NT_HEADERS>(
        reinterpret_cast<uint8_t*>(module) + dosHeader->e_lfanew);

    auto importDir = &ntHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (importDir->Size == 0) return false;

    auto importDesc = reinterpret_cast<PIMAGE_IMPORT_DESCRIPTOR>(
        reinterpret_cast<uint8_t*>(module) + importDir->VirtualAddress);

    bool patched = false;
    for (; importDesc->Name; importDesc++) {
        auto name = reinterpret_cast<const char*>(
            reinterpret_cast<uint8_t*>(module) + importDesc->Name);

        if (_stricmp(name, dllName) != 0) continue;

        auto thunk = reinterpret_cast<PIMAGE_THUNK_DATA>(
            reinterpret_cast<uint8_t*>(module) + importDesc->FirstThunk);

        for (; thunk->u1.Function; thunk++) {
            auto funcPtr = reinterpret_cast<void**>(&thunk->u1.Function);

            if (*funcPtr == origFunc) {
                DWORD oldProtect;
                VirtualProtect(funcPtr, sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect);
                *funcPtr = hookFunc;
                VirtualProtect(funcPtr, sizeof(void*), oldProtect, &oldProtect);
                patched = true;
            }
        }
    }

    return patched;
}

// ─── Get current speed multiplier ─────────────────────────

static float GetSpeed() {
    if (g_sharedData && g_sharedData->enabled) {
        float s = g_sharedData->speedMultiplier;
        if (s > 0.001f && s < 1000.0f) return s;
    }
    return 1.0f;
}

// ─── Hooked functions ─────────────────────────────────────

static BOOL WINAPI HookedQPC(LARGE_INTEGER* lpCounter) {
    LARGE_INTEGER realValue;
    BOOL result = g_origQPC(&realValue);

    if (result && lpCounter) {
        double realDelta = (double)(realValue.QuadPart - g_lastRealQpc.QuadPart);
        g_virtualQpcOffset += realDelta * GetSpeed();
        g_lastRealQpc = realValue;

        lpCounter->QuadPart = g_qpcBase.QuadPart + (LONGLONG)g_virtualQpcOffset;
    }

    return result;
}

static DWORD WINAPI HookedGetTickCount() {
    DWORD realValue = g_origGetTickCount();

    double realDelta = (double)(realValue - g_lastRealTick);
    g_virtualTickOffset += realDelta * GetSpeed();
    g_lastRealTick = realValue;

    return g_tickBase + (DWORD)g_virtualTickOffset;
}

static ULONGLONG WINAPI HookedGetTickCount64() {
    ULONGLONG realValue = g_origGetTickCount64();

    double realDelta = (double)(realValue - g_lastRealTick64);
    g_virtualTick64Offset += realDelta * GetSpeed();
    g_lastRealTick64 = realValue;

    return g_tick64Base + (ULONGLONG)g_virtualTick64Offset;
}

static DWORD WINAPI HookedTimeGetTime() {
    DWORD realValue = g_origTimeGetTime();

    double realDelta = (double)(realValue - g_lastRealTime);
    g_virtualTimeOffset += realDelta * GetSpeed();
    g_lastRealTime = realValue;

    return g_timeBase + (DWORD)g_virtualTimeOffset;
}

// ─── Hook installation ───────────────────────────────────

static void InstallHooks() {
    // Get original function addresses
    HMODULE hKernel32 = GetModuleHandleA("kernel32.dll");
    HMODULE hWinmm = GetModuleHandleA("winmm.dll");
    // Also try ntdll for some games
    HMODULE hNtdll = GetModuleHandleA("ntdll.dll");

    g_origQPC = reinterpret_cast<decltype(g_origQPC)>(
        GetProcAddress(hKernel32, "QueryPerformanceCounter"));
    g_origGetTickCount = reinterpret_cast<decltype(g_origGetTickCount)>(
        GetProcAddress(hKernel32, "GetTickCount"));
    g_origGetTickCount64 = reinterpret_cast<decltype(g_origGetTickCount64)>(
        GetProcAddress(hKernel32, "GetTickCount64"));

    if (hWinmm) {
        g_origTimeGetTime = reinterpret_cast<decltype(g_origTimeGetTime)>(
            GetProcAddress(hWinmm, "timeGetTime"));
    }

    // Capture base values
    if (g_origQPC) {
        g_origQPC(&g_qpcBase);
        g_lastRealQpc = g_qpcBase;
    }
    if (g_origGetTickCount) {
        g_tickBase = g_origGetTickCount();
        g_lastRealTick = g_tickBase;
    }
    if (g_origGetTickCount64) {
        g_tick64Base = g_origGetTickCount64();
        g_lastRealTick64 = g_tick64Base;
    }
    if (g_origTimeGetTime) {
        g_timeBase = g_origTimeGetTime();
        g_lastRealTime = g_timeBase;
    }

    g_baseCaptured = true;

    // Patch IAT of all loaded modules
    HMODULE hModules[1024];
    DWORD cbNeeded;
    HANDLE hProcess = GetCurrentProcess();

    if (EnumProcessModules(hProcess, hModules, sizeof(hModules), &cbNeeded)) {
        int count = cbNeeded / sizeof(HMODULE);
        for (int i = 0; i < count; i++) {
            // Skip system DLLs we're hooking from
            if (hModules[i] == hKernel32 || hModules[i] == hWinmm ||
                hModules[i] == hNtdll) continue;

            // Skip our own DLL
            HMODULE hSelf = nullptr;
            GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                              reinterpret_cast<LPCSTR>(&InstallHooks), &hSelf);
            if (hModules[i] == hSelf) continue;

            if (g_origQPC)
                PatchIAT(hModules[i], "kernel32.dll",
                         (void*)g_origQPC, (void*)HookedQPC);
            if (g_origGetTickCount)
                PatchIAT(hModules[i], "kernel32.dll",
                         (void*)g_origGetTickCount, (void*)HookedGetTickCount);
            if (g_origGetTickCount64)
                PatchIAT(hModules[i], "kernel32.dll",
                         (void*)g_origGetTickCount64, (void*)HookedGetTickCount64);
            if (g_origTimeGetTime && hWinmm)
                PatchIAT(hModules[i], "winmm.dll",
                         (void*)g_origTimeGetTime, (void*)HookedTimeGetTime);
        }
    }
}

// ─── Shared memory connection ─────────────────────────────

static bool ConnectSharedMemory() {
    char name[256];
    snprintf(name, sizeof(name), "MemForge_SpeedHack_%lu", GetCurrentProcessId());

    // Try to open the shared memory created by the main app
    g_hSharedMem = OpenFileMappingA(FILE_MAP_READ, FALSE, name);
    if (!g_hSharedMem) return false;

    g_sharedData = reinterpret_cast<SpeedHackSharedData*>(
        MapViewOfFile(g_hSharedMem, FILE_MAP_READ, 0, 0, sizeof(SpeedHackSharedData)));

    return g_sharedData != nullptr;
}

static void DisconnectSharedMemory() {
    if (g_sharedData) {
        UnmapViewOfFile(g_sharedData);
        g_sharedData = nullptr;
    }
    if (g_hSharedMem) {
        CloseHandle(g_hSharedMem);
        g_hSharedMem = nullptr;
    }
}

// ─── DLL Entry Point ──────────────────────────────────────

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID reserved) {
    switch (reason) {
        case DLL_PROCESS_ATTACH:
            DisableThreadLibraryCalls(hModule);
            if (ConnectSharedMemory()) {
                InstallHooks();
            }
            break;

        case DLL_PROCESS_DETACH:
            // Note: We don't unhook on detach because restoring IAT entries
            // while other threads might be calling them is unsafe.
            // The process is either exiting (safe) or we're being ejected
            // (hooks will point to unloaded memory - but CE does the same).
            DisconnectSharedMemory();
            break;
    }
    return TRUE;
}
