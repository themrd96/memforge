#include "core/hotkey_manager.h"
#include <Windows.h>
#include <sstream>

namespace memforge {

static const char* VkCodeToName(int vk) {
    switch (vk) {
        case VK_F1: return "F1"; case VK_F2: return "F2"; case VK_F3: return "F3";
        case VK_F4: return "F4"; case VK_F5: return "F5"; case VK_F6: return "F6";
        case VK_F7: return "F7"; case VK_F8: return "F8"; case VK_F9: return "F9";
        case VK_F10: return "F10"; case VK_F11: return "F11"; case VK_F12: return "F12";
        case VK_NUMPAD0: return "Num0"; case VK_NUMPAD1: return "Num1";
        case VK_NUMPAD2: return "Num2"; case VK_NUMPAD3: return "Num3";
        case VK_NUMPAD4: return "Num4"; case VK_NUMPAD5: return "Num5";
        case VK_NUMPAD6: return "Num6"; case VK_NUMPAD7: return "Num7";
        case VK_NUMPAD8: return "Num8"; case VK_NUMPAD9: return "Num9";
        case VK_INSERT: return "Insert"; case VK_DELETE: return "Delete";
        case VK_HOME: return "Home"; case VK_END: return "End";
        case VK_PRIOR: return "PageUp"; case VK_NEXT: return "PageDown";
        case VK_PAUSE: return "Pause"; case VK_SCROLL: return "ScrollLock";
        default: break;
    }
    return nullptr;
}

std::string Hotkey::GetKeyString() const {
    std::string result;
    if (ctrl) result += "Ctrl+";
    if (alt) result += "Alt+";
    if (shift) result += "Shift+";

    const char* name = VkCodeToName(vkCode);
    if (name) {
        result += name;
    } else if (vkCode >= 0x30 && vkCode <= 0x5A) {
        result += static_cast<char>(vkCode);
    } else {
        char buf[16];
        snprintf(buf, sizeof(buf), "0x%02X", vkCode);
        result += buf;
    }
    return result;
}

HotkeyManager::HotkeyManager() = default;

HotkeyManager::~HotkeyManager() {
    UnregisterAll();
}

int HotkeyManager::AddHotkey(const Hotkey& hk) {
    Hotkey newHk = hk;
    newHk.id = m_nextId++;
    m_hotkeys.push_back(newHk);

    // Register with OS if enabled and we have a window handle
    if (newHk.enabled && m_hwnd) {
        UINT mods = 0;
        if (newHk.ctrl) mods |= MOD_CONTROL;
        if (newHk.alt) mods |= MOD_ALT;
        if (newHk.shift) mods |= MOD_SHIFT;
        RegisterHotKey(m_hwnd, newHk.id, mods, newHk.vkCode);
    }

    return newHk.id;
}

void HotkeyManager::RemoveHotkey(int id) {
    for (auto it = m_hotkeys.begin(); it != m_hotkeys.end(); ++it) {
        if (it->id == id) {
            if (m_hwnd) {
                UnregisterHotKey(m_hwnd, id);
            }
            m_hotkeys.erase(it);
            return;
        }
    }
}

void HotkeyManager::UpdateHotkey(int id, const Hotkey& hk) {
    for (auto& existing : m_hotkeys) {
        if (existing.id == id) {
            // Unregister old
            if (m_hwnd) {
                UnregisterHotKey(m_hwnd, id);
            }
            existing = hk;
            existing.id = id;
            // Re-register if enabled
            if (existing.enabled && m_hwnd) {
                UINT mods = 0;
                if (existing.ctrl) mods |= MOD_CONTROL;
                if (existing.alt) mods |= MOD_ALT;
                if (existing.shift) mods |= MOD_SHIFT;
                RegisterHotKey(m_hwnd, id, mods, existing.vkCode);
            }
            return;
        }
    }
}

bool HotkeyManager::ProcessMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg != WM_HOTKEY) return false;

    int id = static_cast<int>(wParam);
    for (auto& hk : m_hotkeys) {
        if (hk.id == id && hk.enabled) {
            if (m_callback) {
                m_callback(hk);
            }
            return true;
        }
    }
    return false;
}

void HotkeyManager::SetCallback(HotkeyCallback cb) {
    m_callback = std::move(cb);
}

std::vector<Hotkey>& HotkeyManager::GetHotkeys() {
    return m_hotkeys;
}

const std::vector<Hotkey>& HotkeyManager::GetHotkeys() const {
    return m_hotkeys;
}

void HotkeyManager::SetHwnd(HWND hwnd) {
    m_hwnd = hwnd;
}

void HotkeyManager::RegisterAll() {
    if (!m_hwnd) return;
    for (auto& hk : m_hotkeys) {
        if (hk.enabled) {
            UINT mods = 0;
            if (hk.ctrl) mods |= MOD_CONTROL;
            if (hk.alt) mods |= MOD_ALT;
            if (hk.shift) mods |= MOD_SHIFT;
            RegisterHotKey(m_hwnd, hk.id, mods, hk.vkCode);
        }
    }
}

void HotkeyManager::UnregisterAll() {
    if (!m_hwnd) return;
    for (auto& hk : m_hotkeys) {
        UnregisterHotKey(m_hwnd, hk.id);
    }
}

} // namespace memforge
