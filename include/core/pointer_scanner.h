#pragma once
#include <Windows.h>
#include <cstdint>
#include <string>
#include <vector>
#include <functional>
#include <atomic>
#include <thread>
#include <mutex>

namespace memforge {

struct PointerPath {
    uintptr_t baseAddress;
    std::string moduleName;     // e.g. "game.exe"
    std::vector<int64_t> offsets; // chain of offsets

    // Resolve the path to get the final address
    uintptr_t Resolve(HANDLE hProcess) const;

    // Format as string: "game.exe+0x1234 -> 0x10 -> 0x4C"
    std::string ToString() const;
};

struct PointerScanConfig {
    uintptr_t targetAddress = 0;
    int maxLevel = 5;           // max pointer depth
    int maxOffset = 4096;       // max offset per level
    bool useModuleBases = true; // start from module bases
};

class PointerScanner {
public:
    PointerScanner() = default;
    ~PointerScanner();

    bool StartScan(HANDLE hProcess, DWORD pid, const PointerScanConfig& config,
                   std::function<void(float, size_t)> progressCb = nullptr);
    void CancelScan();

    // Rescan: verify existing paths still resolve to target
    bool Rescan(HANDLE hProcess, uintptr_t newTargetAddress);

    const std::vector<PointerPath>& GetResults() const { return m_results; }
    bool IsScanning() const { return m_scanning.load(); }

private:
    void DoScan(HANDLE hProcess, DWORD pid, PointerScanConfig config,
                std::function<void(float, size_t)> progressCb);

    std::vector<PointerPath> m_results;
    std::atomic<bool> m_scanning{false};
    std::atomic<bool> m_cancelRequested{false};
    std::mutex m_resultsMutex;
    std::thread m_scanThread;
};

} // namespace memforge
