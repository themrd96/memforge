#include "gui/app.h"
#include "core/cheat_table.h"
#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx11.h>
#include <thread>
#include <sstream>
#include <fstream>
// Issue 7: COM file dialog headers
#include <shobjidl.h>
#include <comdef.h>

namespace memforge {

extern void DrawManualMapperPanel(App& app);

App::App() {
    scanConfig.valueType = ValueType::Int32;
    scanConfig.scanMode = ScanMode::ExactValue;
}

App::~App() {
    // Issue 6: Stop jthread scan thread before destruction
    if (m_scanThread.joinable()) {
        m_scanThread.request_stop();
        m_scanThread.join();
    }
    DetachFromProcess();
}

void App::AttachToProcess(DWORD pid) {
    DetachFromProcess();

    targetProcess = ProcessManager::OpenTargetProcess(pid);
    if (!targetProcess) return;

    targetPid = pid;
    processAttached = true;

    // Find the process name
    for (auto& p : processList) {
        if (p.pid == pid) {
            targetName = p.name;
            break;
        }
    }

    scanner.Attach(targetProcess);
    writer.Attach(targetProcess);
    freezer.Attach(targetProcess);
    freezer.Start();

    // Initialize Lua engine with new process
    luaEngine.Initialize(targetProcess, targetPid);

    // Detect game engine
    detectedEngine = EngineDetector::Detect(targetProcess, targetPid);
    engineDetected = (detectedEngine.engine != GameEngine::Unknown);
}

void App::DetachFromProcess() {
    if (!processAttached) return;

    // Issue 6: Stop and join scan thread on detach
    if (m_scanThread.joinable()) {
        m_scanThread.request_stop();
        m_scanThread.join();
    }

    // Eject speedhack if active
    if (speedHack.IsInjected()) {
        speedHack.Eject();
    }

    // Shutdown Lua engine
    luaEngine.Shutdown();

    // Stop network monitoring
    packetInspector.StopMonitoring();

    // Reset engine detection
    engineDetected = false;
    detectedEngine = {};

    freezer.Stop();
    freezer.Detach();
    scanner.Detach();
    writer.Detach();

    if (targetProcess) {
        CloseHandle(targetProcess);
        targetProcess = nullptr;
    }

    targetPid = 0;
    targetName.clear();
    processAttached = false;
    firstScanDone = false;
    speedHackEnabled = false;
    speedValue = 1.0f;

    // Issue 18: Reset stealth state on detach
    stealthApplied = false;
    stealthMgr = StealthManager{};
    stealthConfig = StealthConfig{};
    stealthDetectionCheckTimer = 0.0f;
    stealthHasCheckedDetection = false;
}

void App::RefreshProcessList() {
    processList = ProcessManager::EnumerateProcesses();
    // Issue 15: Precompute lowercase name/title for each process
    for (auto& p : processList) {
        p.nameLower = p.name;
        std::transform(p.nameLower.begin(), p.nameLower.end(),
                       p.nameLower.begin(), ::tolower);
        p.titleLower = p.windowTitle;
        std::transform(p.titleLower.begin(), p.titleLower.end(),
                       p.titleLower.begin(), ::tolower);
    }
}

void App::StartScan() {
    if (scanInProgress) return;

    // Parse scan value from input
    ValueType vt = static_cast<ValueType>(selectedValueType);
    ScanMode sm = static_cast<ScanMode>(selectedScanMode);

    scanConfig.valueType = vt;
    scanConfig.scanMode = sm;

    if (sm != ScanMode::UnknownInitial &&
        sm != ScanMode::Increased &&
        sm != ScanMode::Decreased &&
        sm != ScanMode::Changed &&
        sm != ScanMode::Unchanged) {
        scanConfig.targetValue = MemoryScanner::StringToValue(scanValueInput, vt);
    }

    if (sm == ScanMode::Between) {
        scanConfig.targetValue2 = MemoryScanner::StringToValue(scanValueInput2, vt);
    }

    scanInProgress = true;
    scanProgress = 0.0f;
    scanResultsFound = 0;

    // Issue 6: Use std::jthread stored as member instead of detached std::thread
    m_scanThread = std::jthread([this](std::stop_token st) {
        scanner.FirstScan(scanConfig, [this, &st](float progress, size_t found) {
            if (st.stop_requested()) return;
            scanProgress = progress;
            scanResultsFound = found;
        });
        firstScanDone = true;
        scanInProgress = false;
    });
}

void App::NextScan() {
    if (scanInProgress || !firstScanDone) return;

    ValueType vt = static_cast<ValueType>(selectedValueType);
    ScanMode sm = static_cast<ScanMode>(selectedScanMode);

    scanConfig.valueType = vt;
    scanConfig.scanMode = sm;

    if (sm == ScanMode::ExactValue || sm == ScanMode::GreaterThan ||
        sm == ScanMode::LessThan || sm == ScanMode::IncreasedBy ||
        sm == ScanMode::DecreasedBy) {
        scanConfig.targetValue = MemoryScanner::StringToValue(scanValueInput, vt);
    }

    if (sm == ScanMode::Between) {
        scanConfig.targetValue = MemoryScanner::StringToValue(scanValueInput, vt);
        scanConfig.targetValue2 = MemoryScanner::StringToValue(scanValueInput2, vt);
    }

    scanInProgress = true;
    scanProgress = 0.0f;

    // Issue 6: Use std::jthread stored as member instead of detached std::thread
    m_scanThread = std::jthread([this](std::stop_token st) {
        scanner.NextScan(scanConfig, [this, &st](float progress, size_t found) {
            if (st.stop_requested()) return;
            scanProgress = progress;
            scanResultsFound = found;
        });
        scanInProgress = false;
    });
}

void App::ResetScan() {
    scanner.Reset();
    firstScanDone = false;
    scanProgress = 0.0f;
    scanResultsFound = 0;
    memset(scanValueInput, 0, sizeof(scanValueInput));
    memset(scanValueInput2, 0, sizeof(scanValueInput2));
}

// ─── Menu Bar ────────────────────────────────────────────

void App::DrawMenuBar() {
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Refresh Process List", "F5")) {
                RefreshProcessList();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Save Table", "Ctrl+S")) {
                if (!currentTablePath.empty()) {
                    SaveTable(currentTablePath);
                } else {
                    // Will use a simple path input
                    currentTablePath = "memforge_table.mft";
                    SaveTable(currentTablePath);
                }
            }
            // Issue 7: Use IFileSaveDialog for "Save Table As..."
            if (ImGui::MenuItem("Save Table As...")) {
                IFileSaveDialog* pDlg = nullptr;
                if (SUCCEEDED(CoCreateInstance(CLSID_FileSaveDialog, nullptr,
                                               CLSCTX_INPROC_SERVER,
                                               IID_PPV_ARGS(&pDlg)))) {
                    COMDLG_FILTERSPEC filter[] = {
                        { L"MemForge Table (*.mft)", L"*.mft" },
                        { L"All Files (*.*)", L"*.*" }
                    };
                    pDlg->SetFileTypes(2, filter);
                    pDlg->SetDefaultExtension(L"mft");
                    pDlg->SetFileName(L"memforge_table.mft");
                    if (SUCCEEDED(pDlg->Show(m_hwnd))) {
                        IShellItem* pItem = nullptr;
                        if (SUCCEEDED(pDlg->GetResult(&pItem))) {
                            PWSTR pszPath = nullptr;
                            if (SUCCEEDED(pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszPath))) {
                                int size = WideCharToMultiByte(CP_UTF8, 0, pszPath, -1,
                                                               nullptr, 0, nullptr, nullptr);
                                std::string path(size - 1, '\0');
                                WideCharToMultiByte(CP_UTF8, 0, pszPath, -1,
                                                   path.data(), size, nullptr, nullptr);
                                CoTaskMemFree(pszPath);
                                currentTablePath = path;
                                SaveTable(currentTablePath);
                            }
                            pItem->Release();
                        }
                    }
                    pDlg->Release();
                }
            }
            // Issue 7: Use IFileOpenDialog for "Load Table"
            if (ImGui::MenuItem("Load Table", "Ctrl+O")) {
                IFileOpenDialog* pDlg = nullptr;
                if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr,
                                               CLSCTX_INPROC_SERVER,
                                               IID_PPV_ARGS(&pDlg)))) {
                    COMDLG_FILTERSPEC filter[] = {
                        { L"MemForge Table (*.mft)", L"*.mft" },
                        { L"All Files (*.*)", L"*.*" }
                    };
                    pDlg->SetFileTypes(2, filter);
                    pDlg->SetDefaultExtension(L"mft");
                    if (SUCCEEDED(pDlg->Show(m_hwnd))) {
                        IShellItem* pItem = nullptr;
                        if (SUCCEEDED(pDlg->GetResult(&pItem))) {
                            PWSTR pszPath = nullptr;
                            if (SUCCEEDED(pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszPath))) {
                                int size = WideCharToMultiByte(CP_UTF8, 0, pszPath, -1,
                                                               nullptr, 0, nullptr, nullptr);
                                std::string path(size - 1, '\0');
                                WideCharToMultiByte(CP_UTF8, 0, pszPath, -1,
                                                   path.data(), size, nullptr, nullptr);
                                CoTaskMemFree(pszPath);
                                currentTablePath = path;
                                LoadTable(currentTablePath);
                            }
                            pItem->Release();
                        }
                    }
                    pDlg->Release();
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Exit", "Alt+F4")) {
                PostQuitMessage(0);
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("View")) {
            ImGui::MenuItem("Process Selector", nullptr, &showProcessSelector);
            ImGui::MenuItem("Scanner", nullptr, &showScanner);
            ImGui::MenuItem("Speed Hack", nullptr, &showSpeedHack);
            ImGui::MenuItem("Hex Viewer", nullptr, &showHexViewer);
            ImGui::MenuItem("Stealth Mode", nullptr, &showStealth);
            ImGui::Separator();
            ImGui::MenuItem("Structure Dissector", nullptr, &showStructDissector);
            ImGui::MenuItem("Pointer Scanner", nullptr, &showPointerScanner);
            ImGui::MenuItem("Script Editor", nullptr, &showScriptEditor);
            ImGui::MenuItem("Network Inspector", nullptr, &showNetwork);
            ImGui::Separator();
            ImGui::MenuItem("AOB Scanner", nullptr, &showAobScanner);
            ImGui::MenuItem("Hotkeys", nullptr, &showHotkeys);
            ImGui::MenuItem("Trainer Builder", nullptr, &showTrainerBuilder);
            ImGui::Separator();
            ImGui::MenuItem("Memory Snapshots", nullptr, &showSnapshots);
            ImGui::MenuItem("Undo History", nullptr, &showUndoHistory);
            ImGui::MenuItem("Anti-Anti-Cheat", nullptr, &showAntiCheat);
            ImGui::Separator();
            ImGui::MenuItem("Manual Mapper", nullptr, &showManualMapper);
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Help")) {
            if (ImGui::MenuItem("About MemForge")) {
                showAbout = true;
            }
            ImGui::EndMenu();
        }

        // Right-aligned status — Issue 5: compute widths dynamically
        {
            const float padding = 10.0f;
            std::string statusText;
            if (processAttached) {
                char buf[256];
                snprintf(buf, sizeof(buf), "Attached: %s (PID: %lu)",
                         targetName.c_str(), targetPid);
                statusText = buf;
            } else {
                statusText = "No process attached";
            }
            float statusW = ImGui::CalcTextSize(statusText.c_str()).x + padding;
            ImGui::SetCursorPosX(ImGui::GetWindowWidth() - statusW);
            if (processAttached) {
                ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.3f, 1.0f), "%s", statusText.c_str());
            } else {
                ImGui::TextColored(ImVec4(0.9f, 0.3f, 0.2f, 1.0f), "%s", statusText.c_str());
            }

            if (!ProcessManager::IsElevated()) {
                const char* adminText = "[!] Not Admin";
                float adminW = ImGui::CalcTextSize(adminText).x + padding;
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "%s", adminText);
            }
        }

        ImGui::EndMainMenuBar();
    }
}

// ─── About Window ────────────────────────────────────────

void App::DrawAboutWindow() {
    if (!showAbout) return;
    ImGui::SetNextWindowSize(ImVec2(400, 200), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("About MemForge", &showAbout, ImGuiWindowFlags_NoResize)) {
        ImGui::Text("MemForge v1.0");
        ImGui::Separator();
        ImGui::Text("A modern memory scanner and game tool.");
        ImGui::Spacing();
        ImGui::Text("Features:");
        ImGui::BulletText("Multi-threaded memory scanning");
        ImGui::BulletText("Value freezing");
        ImGui::BulletText("Speed hack (game speed control)");
        ImGui::BulletText("Hex memory viewer");
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
                          "Built with Dear ImGui + DirectX 11");
    }
    ImGui::End();
}

// ─── Status Bar ──────────────────────────────────────────

void App::DrawStatusBar() {
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    float height = 28.0f;

    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x,
                                    viewport->WorkPos.y + viewport->WorkSize.y - height));
    ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x, height));

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 4));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.12f, 0.12f, 0.14f, 1.0f));

    if (ImGui::Begin("##StatusBar", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings)) {

        if (scanInProgress) {
            ImGui::Text("Scanning... %.0f%%  |  Found: %zu",
                       scanProgress * 100.0f, scanResultsFound);
        } else if (firstScanDone) {
            ImGui::Text("Results: %zu", scanner.GetResultCount());
        } else {
            ImGui::Text("Ready");
        }

        // Issue 5: DPI-independent status bar positions using CalcTextSize
        const float padding = 10.0f;
        float winW = ImGui::GetWindowWidth();

        // Engine badge
        if (engineDetected) {
            char engineBuf[128];
            snprintf(engineBuf, sizeof(engineBuf), "[%s]",
                     detectedEngine.engineName.c_str());
            float engineW = ImGui::CalcTextSize(engineBuf).x + padding;
            ImGui::SetCursorPosX(winW - engineW - 250.0f);
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "%s", engineBuf);
            if (ImGui::IsItemHovered() && !detectedEngine.notes.empty()) {
                ImGui::SetTooltip("%s", detectedEngine.notes.c_str());
            }
        }

        // Frozen values count
        if (freezer.IsRunning()) {
            int activeCount = 0;
            for (auto& e : freezer.GetEntries()) {
                if (e.active) activeCount++;
            }
            if (activeCount > 0) {
                char frozenBuf[64];
                snprintf(frozenBuf, sizeof(frozenBuf), "Frozen: %d values", activeCount);
                float frozenW = ImGui::CalcTextSize(frozenBuf).x + padding;
                ImGui::SetCursorPosX(winW - frozenW - 100.0f);
                ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f), "%s", frozenBuf);
            }
        }

        // FPS
        char fpsBuf[32];
        snprintf(fpsBuf, sizeof(fpsBuf), "FPS: %.0f", ImGui::GetIO().Framerate);
        float fpsW = ImGui::CalcTextSize(fpsBuf).x + padding;
        ImGui::SetCursorPosX(winW - fpsW);
        ImGui::Text("%s", fpsBuf);
    }
    ImGui::End();

    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}

// ─── Main Run Loop ───────────────────────────────────────

int App::Run() {
    if (!InitWindow()) return 1;
    if (!InitD3D()) return 1;

    ShowWindow(m_hwnd, SW_SHOWDEFAULT);
    UpdateWindow(m_hwnd);

    // Issue 7: Initialize COM for file dialogs
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    // Setup ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
#ifdef ImGuiConfigFlags_DockingEnable
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
#endif

    // Dark theme with custom colors
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 6.0f;
    style.FrameRounding = 4.0f;
    style.GrabRounding = 4.0f;
    style.TabRounding = 4.0f;
    style.ScrollbarRounding = 4.0f;
    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.PopupRounding = 4.0f;
    style.WindowPadding = ImVec2(10, 10);
    style.FramePadding = ImVec2(6, 4);
    style.ItemSpacing = ImVec2(8, 6);

    // Custom color scheme - dark blue/purple
    ImVec4* colors = style.Colors;
    colors[ImGuiCol_WindowBg] = ImVec4(0.10f, 0.10f, 0.13f, 1.00f);
    colors[ImGuiCol_TitleBg] = ImVec4(0.08f, 0.08f, 0.10f, 1.00f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.15f, 0.12f, 0.22f, 1.00f);
    colors[ImGuiCol_Tab] = ImVec4(0.15f, 0.12f, 0.20f, 1.00f);
    colors[ImGuiCol_TabSelected] = ImVec4(0.28f, 0.20f, 0.45f, 1.00f);
    colors[ImGuiCol_TabHovered] = ImVec4(0.35f, 0.25f, 0.55f, 1.00f);
    colors[ImGuiCol_Header] = ImVec4(0.20f, 0.16f, 0.30f, 1.00f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.30f, 0.22f, 0.45f, 1.00f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.35f, 0.25f, 0.50f, 1.00f);
    colors[ImGuiCol_Button] = ImVec4(0.22f, 0.18f, 0.35f, 1.00f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.35f, 0.28f, 0.55f, 1.00f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.45f, 0.35f, 0.65f, 1.00f);
    colors[ImGuiCol_FrameBg] = ImVec4(0.14f, 0.13f, 0.18f, 1.00f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.20f, 0.18f, 0.28f, 1.00f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.25f, 0.20f, 0.35f, 1.00f);
    colors[ImGuiCol_CheckMark] = ImVec4(0.55f, 0.40f, 0.90f, 1.00f);
    colors[ImGuiCol_SliderGrab] = ImVec4(0.45f, 0.35f, 0.70f, 1.00f);
    colors[ImGuiCol_SliderGrabActive] = ImVec4(0.55f, 0.42f, 0.85f, 1.00f);
    colors[ImGuiCol_ScrollbarBg] = ImVec4(0.08f, 0.08f, 0.10f, 1.00f);
    colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.25f, 0.20f, 0.35f, 1.00f);
    colors[ImGuiCol_PopupBg] = ImVec4(0.12f, 0.11f, 0.16f, 0.96f);
    colors[ImGuiCol_Border] = ImVec4(0.22f, 0.18f, 0.32f, 0.50f);
    colors[ImGuiCol_TableHeaderBg] = ImVec4(0.15f, 0.12f, 0.22f, 1.00f);
    colors[ImGuiCol_TableBorderStrong] = ImVec4(0.20f, 0.16f, 0.30f, 1.00f);
    colors[ImGuiCol_TableBorderLight] = ImVec4(0.15f, 0.12f, 0.22f, 1.00f);

    ImGui_ImplWin32_Init(m_hwnd);
    ImGui_ImplDX11_Init(m_device, m_deviceContext);

    // Setup hotkey manager
    hotkeyManager.SetHwnd(m_hwnd);
    hotkeyManager.SetCallback([this](const Hotkey& hk) {
        switch (hk.action) {
            case HotkeyAction::ToggleFreeze:
                if (hk.freezeId >= 0) {
                    freezer.ToggleEntry(hk.freezeId);
                }
                break;
            case HotkeyAction::RunScript:
                if (!hk.scriptCode.empty()) {
                    luaEngine.Execute(hk.scriptCode);
                }
                break;
            case HotkeyAction::SetSpeed:
                speedValue = hk.speedValue;
                if (speedHack.IsInjected()) {
                    speedHack.SetSpeed(speedValue);
                }
                break;
            case HotkeyAction::ToggleSpeedHack:
                speedHackEnabled = !speedHackEnabled;
                if (speedHackEnabled && !speedHack.IsInjected()) {
                    speedHack.Inject(targetProcess, targetPid);
                    speedHack.SetSpeed(speedValue);
                } else if (!speedHackEnabled && speedHack.IsInjected()) {
                    speedHack.SetSpeed(1.0f);
                }
                break;
            default:
                break;
        }
    });

    // Initial process list
    RefreshProcessList();

    // Main loop
    bool running = true;
    MSG msg = {};
    while (running) {
        while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
            if (msg.message == WM_QUIT) running = false;
        }
        if (!running) break;

        // Check if attached process is still alive
        if (processAttached && targetProcess) {
            if (!ProcessManager::IsProcessRunning(targetProcess)) {
                DetachFromProcess();
            }
        }

        // Start ImGui frame
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        // Enable dockspace (only available in ImGui docking branch)
#ifdef ImGuiConfigFlags_DockingEnable
        if (io.ConfigFlags & ImGuiConfigFlags_DockingEnable) {
            ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(),
                                         ImGuiDockNodeFlags_PassthruCentralNode);
        }
#endif

        // Draw UI
        DrawMenuBar();

        if (showProcessSelector) DrawProcessSelector(*this);
        if (showScanner) DrawScanner(*this);
        if (showSpeedHack) DrawSpeedHack(*this);
        if (showHexViewer) DrawHexViewer(*this);
        if (showStealth) DrawStealth(*this);
        if (showStructDissector) DrawStructureDissector(*this);
        if (showPointerScanner) DrawPointerScanner(*this);
        if (showScriptEditor) DrawScriptEditor(*this);
        if (showNetwork) DrawNetwork(*this);
        if (showAobScanner) DrawAobScanner(*this);
        if (showHotkeys) DrawHotkeys(*this);
        if (showTrainerBuilder) DrawTrainerBuilder(*this);
        if (showSnapshots) DrawSnapshots(*this);
        if (showUndoHistory) DrawUndoHistory(*this);
        if (showAntiCheat) DrawAntiCheat(*this);
        if (showManualMapper) DrawManualMapperPanel(*this);

        DrawAboutWindow();
        DrawStatusBar();

        // Render
        ImGui::Render();
        const float clearColor[4] = { 0.06f, 0.06f, 0.08f, 1.0f };
        m_deviceContext->OMSetRenderTargets(1, &m_rtv, nullptr);
        m_deviceContext->ClearRenderTargetView(m_rtv, clearColor);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        m_swapChain->Present(1, 0); // vsync on
    }

    // Cleanup
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    CleanupD3D();
    DestroyWindow(m_hwnd);

    CoUninitialize();

    return 0;
}

// ─── Cheat Table Save/Load ───────────────────────────────

void App::SaveTable(const std::string& path) {
    CheatTable table;
    table.gameName = targetName;
    table.gameExe = targetName;
    table.description = "MemForge cheat table";
    table.author = "MemForge";
    table.version = "1.0";

    // Save frozen values
    table.frozenValues = freezer.GetEntries();

    // Save current structure if any
    if (!currentStruct.fields.empty()) {
        table.structures.push_back(currentStruct);
    }

    // Save pointer scan results
    table.pointerPaths = pointerScanner.GetResults();

    // Save current script
    if (scriptEditorText[0] != '\0') {
        CheatTable::SavedScript script;
        script.name = "Main Script";
        script.code = scriptEditorText;
        table.scripts.push_back(script);
    }

    table.SaveToFile(path);
    currentTablePath = path;
    tableModified = false;
}

void App::LoadTable(const std::string& path) {
    auto table = CheatTable::LoadFromFile(path);
    if (!table) return;

    // Restore frozen values
    for (auto& fv : table->frozenValues) {
        freezer.AddEntry(fv.address, fv.value, fv.type, fv.description);
    }

    // Restore first structure
    if (!table->structures.empty()) {
        currentStruct = table->structures[0];
    }

    // Restore first script
    if (!table->scripts.empty()) {
        strncpy(scriptEditorText, table->scripts[0].code.c_str(),
                sizeof(scriptEditorText) - 1);
        scriptEditorText[sizeof(scriptEditorText) - 1] = '\0';
    }

    currentTablePath = path;
    tableModified = false;
}

} // namespace memforge
