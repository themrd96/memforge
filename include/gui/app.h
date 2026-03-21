#pragma once
#include <Windows.h>
#include <d3d11.h>
#include <string>
#include <memory>
#include <vector>

#include "core/process_manager.h"
#include "core/memory_scanner.h"
#include "core/memory_writer.h"
#include "core/value_freezer.h"
#include "core/structure_dissector.h"
#include "core/pointer_scanner.h"
#include "core/lua_engine.h"
#include "core/engine_detector.h"
#include "core/packet_inspector.h"
#include "speedhack/speedhack.h"

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
