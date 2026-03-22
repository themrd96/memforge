#pragma once
#include <Windows.h>
#include <vector>
#include <string>
#include <functional>
#include <cstdint>
#include <mutex>
#include <atomic>
#include <thread>
#include <variant>
#include <condition_variable>

namespace memforge {

// Value types the scanner can handle
enum class ValueType {
    Int8,
    Int16,
    Int32,
    Int64,
    Float,
    Double,
    ByteArray,
    String
};

// Scan comparison modes
enum class ScanMode {
    ExactValue,
    GreaterThan,
    LessThan,
    Between,
    UnknownInitial,
    Increased,
    Decreased,
    Changed,
    Unchanged,
    IncreasedBy,
    DecreasedBy
};

// Represents a value that can be any supported type
using ScanValue = std::variant<
    int8_t, int16_t, int32_t, int64_t,
    float, double,
    std::vector<uint8_t>,
    std::string
>;

// A single scan result
struct ScanResult {
    uintptr_t address;
    ScanValue currentValue;
    ScanValue previousValue;
};

// Memory region info
struct MemoryRegion {
    uintptr_t baseAddress;
    SIZE_T size;
    DWORD protect;
    DWORD state;
    DWORD type;
};

// Scanner configuration
struct ScanConfig {
    ValueType valueType = ValueType::Int32;
    ScanMode scanMode = ScanMode::ExactValue;
    ScanValue targetValue;
    ScanValue targetValue2; // for "Between" mode
    bool alignedScan = true;
    bool writable = true;
    bool executable = false;
    uintptr_t startAddress = 0;
    uintptr_t endAddress = 0x7FFFFFFFFFFF; // user-space limit on x64
};

// Progress callback
using ScanProgressCallback = std::function<void(float progress, size_t resultsFound)>;

class MemoryScanner {
public:
    MemoryScanner();
    ~MemoryScanner();

    // Attach to a process
    bool Attach(HANDLE hProcess);
    void Detach();

    // First scan - searches all memory regions
    bool FirstScan(const ScanConfig& config, ScanProgressCallback progressCb = nullptr);

    // Next scan - filters existing results
    bool NextScan(const ScanConfig& config, ScanProgressCallback progressCb = nullptr);

    // Reset scanner (clear all results)
    void Reset();

    // Cancel an ongoing scan
    void CancelScan();

    // Get results
    const std::vector<ScanResult>& GetResults() const { return m_results; }
    size_t GetResultCount() const { return m_results.size(); }

    // Read a fresh value at an address
    ScanValue ReadValue(uintptr_t address, ValueType type);

    // Get all readable memory regions
    std::vector<MemoryRegion> GetMemoryRegions() const;

    bool IsScanning() const { return m_scanning.load(); }

    // Get size of a value type in bytes
    static size_t GetValueSize(ValueType type);

    // Value to string for display
    static std::string ValueToString(const ScanValue& val, ValueType type);

    // String to value for input
    static ScanValue StringToValue(const std::string& str, ValueType type);

private:
    HANDLE m_hProcess = nullptr;
    std::vector<ScanResult> m_results;
    std::atomic<bool> m_scanning{false};
    std::atomic<bool> m_cancelRequested{false};
    std::mutex m_resultsMutex;
    // Issue 8: Condition variable for CancelScan to wait on instead of busy-wait
    std::mutex m_scanDoneMutex;
    std::condition_variable m_scanDoneCV;

    // Internal scan helpers
    void ScanRegion(const MemoryRegion& region, const ScanConfig& config,
                    std::vector<ScanResult>& localResults);

    bool CompareValue(const uint8_t* data, size_t offset,
                      const ScanConfig& config) const;

    bool CompareValues(const ScanValue& current, const ScanValue& previous,
                       const ScanConfig& config) const;

    template<typename T>
    bool CompareNumeric(T current, T target, ScanMode mode, T target2 = T{}) const;
};

} // namespace memforge
