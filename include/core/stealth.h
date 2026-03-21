#pragma once
#include <Windows.h>
#include <string>
#include <vector>
#include <random>
#include <functional>

namespace memforge {

// Stealth configuration
struct StealthConfig {
    bool randomizeWindowTitle = true;
    bool randomizeClassName = false;    // requires restart
    bool hiddenFromTaskbar = false;
    bool mutexCloaking = true;          // prevent mutex-based detection
    bool clearPeHeaders = false;        // wipe PE header from own memory
    bool blockWindowEnumeration = true; // hide from EnumWindows-based detection
    std::string customWindowTitle;      // empty = auto-generated
};

// Generates random strings that look like legitimate Windows processes
class NameGenerator {
public:
    static std::string GenerateWindowTitle();
    static std::string GenerateClassName();
    static std::string GenerateExeName();
    static std::string GetDecoyName();

private:
    static std::string RandomString(int minLen, int maxLen);
    static std::mt19937& GetRNG();
};

class StealthManager {
public:
    StealthManager();
    ~StealthManager();

    // Apply stealth measures to the current process
    void Apply(HWND hwnd, const StealthConfig& config);

    // Remove stealth measures (restore original state)
    void Restore();

    // Individual stealth features
    void RandomizeWindowTitle(HWND hwnd, const std::string& customTitle = "");
    void HideFromTaskbar(HWND hwnd, bool hide);
    void ClearPEHeaders();
    void CleanupMutexes();
    void BlockWindowEnumeration(HWND hwnd, bool block);

    // Self-rename: copy exe to temp with random name and relaunch
    static bool RelaunchWithRandomName(int argc = 0, char** argv = nullptr);
    static bool IsRunningRandomized();

    bool IsActive() const { return m_active; }
    std::string GetCurrentWindowTitle() const { return m_currentTitle; }
    std::string GetOriginalWindowTitle() const { return m_originalTitle; }

    // Anti-detection: check if common analysis tools are running
    struct DetectionStatus {
        bool debuggerAttached = false;
        bool analysisToolsRunning = false;
        std::vector<std::string> detectedTools;
    };
    static DetectionStatus CheckForDetection();

private:
    bool m_active = false;
    HWND m_hwnd = nullptr;
    std::string m_originalTitle;
    std::string m_currentTitle;
    std::string m_originalClassName;
    LONG_PTR m_originalExStyle = 0;
    StealthConfig m_config;
    std::vector<uint8_t> m_originalPEHeader;
};

} // namespace memforge
