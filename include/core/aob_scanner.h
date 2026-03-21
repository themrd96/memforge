#pragma once
#include <Windows.h>
#include <cstdint>
#include <vector>
#include <string>
#include <functional>
#include <atomic>
#include <map>

namespace memforge {

struct AobResult {
    uintptr_t address;
    std::vector<uint8_t> matchedBytes;
};

class AobScanner {
public:
    // Parse a pattern string like "89 ?? 4C 02 00 00" into bytes and mask
    static bool ParsePattern(const std::string& patternStr,
                            std::vector<uint8_t>& bytes,
                            std::vector<bool>& mask);

    // Scan process memory for a byte pattern
    bool Scan(HANDLE hProcess, const std::string& pattern,
              std::function<void(float, size_t)> progressCb = nullptr);

    // Scan within a specific module only
    bool ScanModule(HANDLE hProcess, const std::string& moduleName,
                    const std::string& pattern);

    void Cancel();
    bool IsScanning() const;

    const std::vector<AobResult>& GetResults() const;
    void Reset();

    // NOP out bytes at an address (replace with 0x90)
    static bool NopAt(HANDLE hProcess, uintptr_t address, size_t count);

    // Restore original bytes (saved from scan)
    bool RestoreAt(HANDLE hProcess, uintptr_t address);

private:
    std::vector<AobResult> m_results;
    std::atomic<bool> m_scanning{false};
    std::atomic<bool> m_cancelRequested{false};

    // Store original bytes for restore
    std::map<uintptr_t, std::vector<uint8_t>> m_originalBytes;
};

} // namespace memforge
