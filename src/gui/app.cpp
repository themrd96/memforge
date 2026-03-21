#include "gui/app.h"
#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx11.h>
#include <thread>
#include <sstream>

namespace memforge {

extern App* g_appInstance;

App::App() {
    g_appInstance = this;
    scanConfig.valueType = ValueType::Int32;
    scanConfig.scanMode = ScanMode::ExactValue;
}

App::~App() {
    DetachFromProcess();
    g_appInstance = nullptr;
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
}

void App::DetachFromProcess() {
    if (!processAttached) return;

    // Eject speedhack if active
    if (speedHack.IsInjected()) {
        speedHack.Eject();
    }

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
}

void App::RefreshProcessList() {
    processList = ProcessManager::EnumerateProcesses();
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

    // Run scan in background thread
    std::thread([this]() {
        scanner.FirstScan(scanConfig, [this](float progress, size_t found) {
            scanProgress = progress;
            scanResultsFound = found;
        });
        firstScanDone = true;
        scanInProgress = false;
    }).detach();
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

    std::thread([this]() {
        scanner.NextScan(scanConfig, [this](float progress, size_t found) {
            scanProgress = progress;
            scanResultsFound = found;
        });
        scanInProgress = false;
    }).detach();
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
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Help")) {
            if (ImGui::MenuItem("About MemForge")) {
                showAbout = true;
            }
            ImGui::EndMenu();
        }

        // Right-aligned status
        float statusWidth = 400.0f;
        ImGui::SameLine(ImGui::GetWindowWidth() - statusWidth);

        if (processAttached) {
            ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.3f, 1.0f),
                             "Attached: %s (PID: %lu)", targetName.c_str(), targetPid);
        } else {
            ImGui::TextColored(ImVec4(0.9f, 0.3f, 0.2f, 1.0f), "No process attached");
        }

        if (!ProcessManager::IsElevated()) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "[!] Not Admin");
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

        ImGui::SameLine(ImGui::GetWindowWidth() - 250);
        if (freezer.IsRunning()) {
            int activeCount = 0;
            for (auto& e : freezer.GetEntries()) {
                if (e.active) activeCount++;
            }
            if (activeCount > 0) {
                ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f),
                                 "Frozen: %d values", activeCount);
            }
        }

        ImGui::SameLine(ImGui::GetWindowWidth() - 120);
        ImGui::Text("FPS: %.0f", ImGui::GetIO().Framerate);
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

    // Setup ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

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

        // Enable dockspace
        ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(),
                                     ImGuiDockNodeFlags_PassthruCentralNode);

        // Draw UI
        DrawMenuBar();

        if (showProcessSelector) DrawProcessSelector(*this);
        if (showScanner) DrawScanner(*this);
        if (showSpeedHack) DrawSpeedHack(*this);
        if (showHexViewer) DrawHexViewer(*this);
        if (showStealth) DrawStealth(*this);

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

    return 0;
}

} // namespace memforge
