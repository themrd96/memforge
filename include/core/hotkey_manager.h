#pragma once
#include <Windows.h>
#include <string>
#include <vector>
#include <functional>
#include <cstdint>

namespace memforge {

enum class HotkeyAction {
    ToggleFreeze,
    RunScript,
    SetSpeed,
    ToggleSpeedHack,
    Custom
};

struct Hotkey {
    int id = 0;
    int vkCode = 0;
    bool ctrl = false;
    bool alt = false;
    bool shift = false;
    HotkeyAction action = HotkeyAction::ToggleFreeze;
    std::string description;

    // Action-specific data
    int freezeId = -1;
    std::string scriptCode;
    float speedValue = 1.0f;
    bool enabled = true;

    std::string GetKeyString() const;
};

class HotkeyManager {
public:
    HotkeyManager();
    ~HotkeyManager();

    int AddHotkey(const Hotkey& hk);
    void RemoveHotkey(int id);
    void UpdateHotkey(int id, const Hotkey& hk);

    bool ProcessMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    using HotkeyCallback = std::function<void(const Hotkey&)>;
    void SetCallback(HotkeyCallback cb);

    std::vector<Hotkey>& GetHotkeys();
    const std::vector<Hotkey>& GetHotkeys() const;

    void SetHwnd(HWND hwnd);
    void RegisterAll();
    void UnregisterAll();

private:
    std::vector<Hotkey> m_hotkeys;
    HotkeyCallback m_callback;
    int m_nextId = 1;
    HWND m_hwnd = nullptr;
};

} // namespace memforge
