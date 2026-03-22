/*
 * DirectX 11 renderer setup for ImGui.
 * Handles window creation, D3D11 device/swap chain, and the render loop.
 */

#include "gui/app.h"
#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx11.h>
#include <dwmapi.h>

// Issue 3: Named constant for dark mode attribute instead of magic number 20
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

// Forward declare message handler from imgui_impl_win32.cpp
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
    HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace memforge {

// Issue 4: Removed global g_appInstance singleton.
// App pointer is now stored per-window via SetWindowLongPtr(GWLP_USERDATA).

static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    // Issue 4: Retrieve App pointer from window user data instead of global
    App* pApp = reinterpret_cast<App*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));

    switch (msg) {
        case WM_SIZE:
            if (pApp && wParam != SIZE_MINIMIZED) {
                pApp->HandleResize(LOWORD(lParam), HIWORD(lParam));
            }
            return 0;
        case WM_HOTKEY:
            if (pApp) {
                pApp->hotkeyManager.ProcessMessage(msg, wParam, lParam);
            }
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcA(hWnd, msg, wParam, lParam);
}

bool App::InitWindow() {
    WNDCLASSEXA wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleA(nullptr);
    wc.lpszClassName = "MemForgeWindow";
    wc.hIcon = LoadIconA(nullptr, IDI_APPLICATION);
    RegisterClassExA(&wc);

    m_hwnd = CreateWindowExA(
        0, "MemForgeWindow", "MemForge v1.0",
        WS_OVERLAPPEDWINDOW,
        100, 100, 1280, 800,
        nullptr, nullptr, wc.hInstance, nullptr
    );

    if (!m_hwnd) return false;

    // Issue 4: Store App pointer in the window's user data so WndProc can retrieve it
    SetWindowLongPtr(m_hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

    // Issue 3: Use named constant DWMWA_USE_IMMERSIVE_DARK_MODE instead of 20
    BOOL darkMode = TRUE;
    DwmSetWindowAttribute(m_hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE,
                          &darkMode, sizeof(darkMode));

    return true;
}

bool App::InitD3D() {
    DXGI_SWAP_CHAIN_DESC sd = {};
    // Issue 2: BufferCount = 2 required for FLIP_DISCARD
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = m_hwnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    // Issue 2: Use FLIP_DISCARD instead of DISCARD for better performance
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    UINT createFlags = 0;
#ifdef _DEBUG
    createFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    D3D_FEATURE_LEVEL featureLevel;
    D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_11_0 };

    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        createFlags, featureLevels, 1,
        D3D11_SDK_VERSION, &sd,
        &m_swapChain, &m_device, &featureLevel, &m_deviceContext
    );

    if (FAILED(hr)) return false;

    CreateRTV();
    return true;
}

void App::CreateRTV() {
    ID3D11Texture2D* backBuffer = nullptr;
    m_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    if (backBuffer) {
        m_device->CreateRenderTargetView(backBuffer, nullptr, &m_rtv);
        backBuffer->Release();
    }
}

void App::CleanupD3D() {
    if (m_rtv) { m_rtv->Release(); m_rtv = nullptr; }
    if (m_swapChain) { m_swapChain->Release(); m_swapChain = nullptr; }
    if (m_deviceContext) { m_deviceContext->Release(); m_deviceContext = nullptr; }
    if (m_device) { m_device->Release(); m_device = nullptr; }
}

void App::HandleResize(UINT width, UINT height) {
    if (width == 0 || height == 0) return;
    if (m_rtv) { m_rtv->Release(); m_rtv = nullptr; }
    m_swapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
    CreateRTV();
}

} // namespace memforge
