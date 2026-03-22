#pragma once
#include <Windows.h>
#include <d3d11.h>
#include <string>
#include <memory>
#include <vector>
#include <thread>

#include "core/process_manager.h"
#include "core/memory_scanner.h"
#include "core/memory_writer.h"
#include "core/value_freezer.h"
#include "core/structure_dissector.h"
#include "core/pointer_scanner.h"
#include "core/lua_engine.h"
#include "core/engine_detector.h"
#include "core/packet_inspector.h"
#include "core/aob_scanner.h"
#include "core/hotkey_manager.h"
#include "core/trainer_builder.h"
#include "core/memory_snapshot.h"
#include "core/undo_history.h"
#include "core/anti_anticheat.h"
#include "speedhack/speedhack.h"
#include "core/stealth.h"
#include "core/manual_mapper.h"

namespace memforge {

// Forward declarations for UI panels
void DrawProcessSelector(class App& app);
void DrawScanner(class App& app);
void DrawSpeedHack(class App& app);
void DrawHexViewer(class App& app);
void DrawStealth(class App& app);
void DrawStructureDissector(class App& app);
void DrawPointerScanner(class App& app);
void DrawScriptEditor(class App& app);
void DrawNetwork(class App& app);
void DrawAobScanner(class App& app);
void DrawHotkeys(class App& app);
void DrawTrainerBuilder(class App& app);
void DrawSnapshots(class App& app);
void DrawUndoHistory(class App& app);
void DrawAntiCheat(class App& app);

class App {
public:
    App();
    ~App();

    // Main loop
    int Run();

    // State - accessible by UI panels
    // Process
    HANDLE targetProcess = nullptr;
    DWORD targetPid = 0;
    std::string targetName;
    std::vector<ProcessInfo> processList;
    bool processAttached = false;
    char processFilter[256] = {};
    AttachMethod lastAttachMethod = AttachMethod::Normal; // how we opened the process

    // Scanner
    MemoryScanner scanner;
    MemoryWriter writer;
    ValueFreezer freezer;
    ScanConfig scanConfig;
    bool firstScanDone = false;
    char scanValueInput[256] = {};
    char scanValueInput2[256] = {};
    float scanProgress = 0.0f;
    size_t scanResultsFound = 0;
    bool scanInProgress = false;
    int selectedValueType = 2; // default Int32
    int selectedScanMode = 0; // default ExactValue

    // Speed hack
    SpeedHackController speedHack;
    float speedValue = 1.0f;
    bool speedHackEnabled = false;

    // Hex viewer
    uintptr_t hexViewAddress = 0;
    char hexAddrInput[64] = {};
    int hexViewColumns = 16;
    int hexViewRows = 32;
    // Issue 17: Hex viewer read cache
    std::vector<uint8_t> hexCacheBuffer;
    uintptr_t hexCacheAddress = ~uintptr_t(0);
    int hexCacheColumns = 0;
    int hexCacheRows = 0;
    int hexCacheFrameCounter = 0;

    // Structure Dissector
    StructureDissector structDissector;
    StructDefinition currentStruct;
    char structAddrInput[64] = {};

    // Pointer Scanner
    PointerScanner pointerScanner;
    char ptrScanAddrInput[64] = {};
    int ptrScanMaxLevel = 5;
    int ptrScanMaxOffset = 4096;
    float ptrScanProgress = 0.0f;
    size_t ptrScanResultCount = 0;

    // Lua Scripting
    LuaEngine luaEngine;
    char scriptEditorText[65536] = {};
    std::string scriptConsoleOutput;
    bool showScriptLoadDialog = false;
    bool showScriptSaveDialog = false;
    bool showScriptApiRef = false;

    // Engine Detection
    EngineInfo detectedEngine;
    bool engineDetected = false;

    // Network Inspector
    PacketInspector packetInspector;
    std::vector<NetworkConnection> networkConnections;

    // Cheat Table
    std::string currentTablePath;
    bool tableModified = false;

    // AOB Scanner
    AobScanner aobScanner;
    char aobPatternInput[512] = {};
    float aobScanProgress = 0.0f;
    size_t aobResultCount = 0;

    // Hotkey Manager
    HotkeyManager hotkeyManager;

    // Trainer Builder
    std::vector<TrainerCheat> trainerCheats;
    char trainerGameName[256] = {};
    char trainerGameExe[256] = {};
    char trainerName[256] = {};
    char trainerAuthor[256] = {};
    char trainerOutputPath[512] = {};
    std::string trainerBuildLog;

    // Memory Snapshots
    MemorySnapshot snapshotA;
    MemorySnapshot snapshotB;
    std::vector<MemoryDiff> snapshotDiffs;

    // Undo History
    UndoHistory undoHistory;

    // Issue 18: Stealth state moved from file-scope statics in ui_stealth.cpp to App members
    StealthManager stealthMgr;
    StealthConfig stealthConfig;
    char stealthCustomTitle[256] = {};
    bool stealthApplied = false;
    float stealthDetectionCheckTimer = 0.0f;
    StealthManager::DetectionStatus stealthLastDetection;
    bool stealthHasCheckedDetection = false;

    // Manual Mapper
    ManualMapper manualMapper;
    uintptr_t mappedBase = 0;
    std::vector<std::string> mapperLog;
    bool mapperSuccess = false;
    std::string mapperError;
    std::string mapperDllPath;

    // Issue 6: jthread for scan operations (stored as member for lifecycle management)
    std::jthread m_scanThread;

    // Permanently suspended game thread handles.
    // Held for the entire attachment lifetime so the in-game watchdog thread
    // can never call NtQuerySystemInformation and detect our handle.
    // ReadProcessMemory / WriteProcessMemory work fine on suspended processes.
    std::vector<HANDLE> m_suspendedGameThreads;

    // Watches for newly spawned game threads and suspends them immediately.
    // Prevents the watchdog from re-spawning on a fresh thread after we
    // freeze the initial thread set.
    std::jthread m_threadWatcher;

    // UI state
    bool showProcessSelector = true;
    bool showScanner = true;
    bool showSpeedHack = true;
    bool showHexViewer = false;
    bool showStealth = false;
    bool showAbout = false;
    bool showStructDissector = false;
    bool showPointerScanner = false;
    bool showScriptEditor = false;
    bool showNetwork = false;
    bool showAobScanner = false;
    bool showHotkeys = false;
    bool showTrainerBuilder = false;
    bool showSnapshots = false;
    bool showUndoHistory = false;
    bool showAntiCheat = false;
    bool showManualMapper = false;

    // Methods
    void AttachToProcess(DWORD pid);
    void DetachFromProcess();
    void RefreshProcessList();
    void StartScan();
    void NextScan();
    void ResetScan();

    void HandleResize(UINT width, UINT height);

    // Cheat table
    void SaveTable(const std::string& path);
    void LoadTable(const std::string& path);

private:
    void DrawMenuBar();
    void DrawStatusBar();
    void DrawAboutWindow();

    // DX11 / Window
    HWND m_hwnd = nullptr;
    ID3D11Device* m_device = nullptr;
    ID3D11DeviceContext* m_deviceContext = nullptr;
    IDXGISwapChain* m_swapChain = nullptr;
    ID3D11RenderTargetView* m_rtv = nullptr;

    bool InitWindow();
    bool InitD3D();
    void CleanupD3D();
    void CreateRTV();
};

} // namespace memforge
