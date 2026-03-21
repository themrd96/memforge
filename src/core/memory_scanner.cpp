#include "core/memory_scanner.h"
#include <cstring>
#include <cmath>
#include <sstream>
#include <iomanip>
#include <algorithm>

namespace memforge {

MemoryScanner::MemoryScanner() {}
MemoryScanner::~MemoryScanner() { CancelScan(); }

bool MemoryScanner::Attach(HANDLE hProcess) {
    m_hProcess = hProcess;
    return hProcess != nullptr;
}

void MemoryScanner::Detach() {
    CancelScan();
    Reset();
    m_hProcess = nullptr;
}

void MemoryScanner::Reset() {
    std::lock_guard<std::mutex> lock(m_resultsMutex);
    m_results.clear();
}

void MemoryScanner::CancelScan() {
    m_cancelRequested.store(true);
    // Wait for scan to finish
    while (m_scanning.load()) {
        Sleep(10);
    }
    m_cancelRequested.store(false);
}

// ─── Get readable memory regions ──────────────────────────

std::vector<MemoryRegion> MemoryScanner::GetMemoryRegions() const {
    std::vector<MemoryRegion> regions;
    if (!m_hProcess) return regions;

    MEMORY_BASIC_INFORMATION mbi{};
    uintptr_t address = 0;

    while (VirtualQueryEx(m_hProcess, reinterpret_cast<LPCVOID>(address),
                          &mbi, sizeof(mbi))) {
        if (mbi.State == MEM_COMMIT) {
            bool readable = (mbi.Protect & PAGE_READONLY) ||
                           (mbi.Protect & PAGE_READWRITE) ||
                           (mbi.Protect & PAGE_WRITECOPY) ||
                           (mbi.Protect & PAGE_EXECUTE_READ) ||
                           (mbi.Protect & PAGE_EXECUTE_READWRITE) ||
                           (mbi.Protect & PAGE_EXECUTE_WRITECOPY);

            bool guarded = (mbi.Protect & PAGE_GUARD) || (mbi.Protect & PAGE_NOACCESS);

            if (readable && !guarded) {
                MemoryRegion region;
                region.baseAddress = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
                region.size = mbi.RegionSize;
                region.protect = mbi.Protect;
                region.state = mbi.State;
                region.type = mbi.Type;
                regions.push_back(region);
            }
        }

        address = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
        if (address == 0) break; // overflow protection
    }

    return regions;
}

// ─── Value size ──────────────────────────────────────────

size_t MemoryScanner::GetValueSize(ValueType type) {
    switch (type) {
        case ValueType::Int8:    return 1;
        case ValueType::Int16:   return 2;
        case ValueType::Int32:   return 4;
        case ValueType::Int64:   return 8;
        case ValueType::Float:   return 4;
        case ValueType::Double:  return 8;
        default:                 return 4;
    }
}

// ─── Read a value from memory ────────────────────────────

ScanValue MemoryScanner::ReadValue(uintptr_t address, ValueType type) {
    uint8_t buffer[8] = {};
    SIZE_T bytesRead = 0;

    ReadProcessMemory(m_hProcess, reinterpret_cast<LPCVOID>(address),
                      buffer, GetValueSize(type), &bytesRead);

    switch (type) {
        case ValueType::Int8:   return *reinterpret_cast<int8_t*>(buffer);
        case ValueType::Int16:  return *reinterpret_cast<int16_t*>(buffer);
        case ValueType::Int32:  return *reinterpret_cast<int32_t*>(buffer);
        case ValueType::Int64:  return *reinterpret_cast<int64_t*>(buffer);
        case ValueType::Float:  return *reinterpret_cast<float*>(buffer);
        case ValueType::Double: return *reinterpret_cast<double*>(buffer);
        default:                return int32_t(0);
    }
}

// ─── Scan a single memory region ─────────────────────────

void MemoryScanner::ScanRegion(const MemoryRegion& region, const ScanConfig& config,
                                std::vector<ScanResult>& localResults) {
    size_t valueSize = GetValueSize(config.valueType);
    size_t alignment = config.alignedScan ? valueSize : 1;

    // Read entire region at once for performance
    std::vector<uint8_t> buffer(region.size);
    SIZE_T bytesRead = 0;
    if (!ReadProcessMemory(m_hProcess, reinterpret_cast<LPCVOID>(region.baseAddress),
                           buffer.data(), region.size, &bytesRead)) {
        return;
    }

    for (size_t offset = 0; offset + valueSize <= bytesRead; offset += alignment) {
        if (m_cancelRequested.load()) return;

        if (CompareValue(buffer.data(), offset, config)) {
            ScanResult result;
            result.address = region.baseAddress + offset;

            // Extract the current value
            switch (config.valueType) {
                case ValueType::Int8:
                    result.currentValue = *reinterpret_cast<int8_t*>(&buffer[offset]);
                    break;
                case ValueType::Int16:
                    result.currentValue = *reinterpret_cast<int16_t*>(&buffer[offset]);
                    break;
                case ValueType::Int32:
                    result.currentValue = *reinterpret_cast<int32_t*>(&buffer[offset]);
                    break;
                case ValueType::Int64:
                    result.currentValue = *reinterpret_cast<int64_t*>(&buffer[offset]);
                    break;
                case ValueType::Float:
                    result.currentValue = *reinterpret_cast<float*>(&buffer[offset]);
                    break;
                case ValueType::Double:
                    result.currentValue = *reinterpret_cast<double*>(&buffer[offset]);
                    break;
                default:
                    result.currentValue = *reinterpret_cast<int32_t*>(&buffer[offset]);
                    break;
            }

            result.previousValue = result.currentValue;
            localResults.push_back(result);
        }
    }
}

// ─── Compare value against scan criteria ─────────────────

bool MemoryScanner::CompareValue(const uint8_t* data, size_t offset,
                                  const ScanConfig& config) const {
    if (config.scanMode == ScanMode::UnknownInitial) return true;

    switch (config.valueType) {
        case ValueType::Int8: {
            int8_t val = *reinterpret_cast<const int8_t*>(&data[offset]);
            int8_t target = std::get<int8_t>(config.targetValue);
            int8_t target2 = (config.scanMode == ScanMode::Between)
                             ? std::get<int8_t>(config.targetValue2) : int8_t{};
            return CompareNumeric(val, target, config.scanMode, target2);
        }
        case ValueType::Int16: {
            int16_t val = *reinterpret_cast<const int16_t*>(&data[offset]);
            int16_t target = std::get<int16_t>(config.targetValue);
            int16_t target2 = (config.scanMode == ScanMode::Between)
                              ? std::get<int16_t>(config.targetValue2) : int16_t{};
            return CompareNumeric(val, target, config.scanMode, target2);
        }
        case ValueType::Int32: {
            int32_t val = *reinterpret_cast<const int32_t*>(&data[offset]);
            int32_t target = std::get<int32_t>(config.targetValue);
            int32_t target2 = (config.scanMode == ScanMode::Between)
                              ? std::get<int32_t>(config.targetValue2) : int32_t{};
            return CompareNumeric(val, target, config.scanMode, target2);
        }
        case ValueType::Int64: {
            int64_t val = *reinterpret_cast<const int64_t*>(&data[offset]);
            int64_t target = std::get<int64_t>(config.targetValue);
            int64_t target2 = (config.scanMode == ScanMode::Between)
                              ? std::get<int64_t>(config.targetValue2) : int64_t{};
            return CompareNumeric(val, target, config.scanMode, target2);
        }
        case ValueType::Float: {
            float val = *reinterpret_cast<const float*>(&data[offset]);
            float target = std::get<float>(config.targetValue);
            float target2 = (config.scanMode == ScanMode::Between)
                            ? std::get<float>(config.targetValue2) : float{};
            return CompareNumeric(val, target, config.scanMode, target2);
        }
        case ValueType::Double: {
            double val = *reinterpret_cast<const double*>(&data[offset]);
            double target = std::get<double>(config.targetValue);
            double target2 = (config.scanMode == ScanMode::Between)
                             ? std::get<double>(config.targetValue2) : double{};
            return CompareNumeric(val, target, config.scanMode, target2);
        }
        default:
            return false;
    }
}

// ─── Compare two values (for next scan) ──────────────────

bool MemoryScanner::CompareValues(const ScanValue& current, const ScanValue& previous,
                                   const ScanConfig& config) const {
    auto doCompare = [&](auto curr, auto prev) -> bool {
        using T = decltype(curr);
        switch (config.scanMode) {
            case ScanMode::ExactValue:
                return CompareNumeric(curr, std::get<T>(config.targetValue), ScanMode::ExactValue);
            case ScanMode::GreaterThan:
                return CompareNumeric(curr, std::get<T>(config.targetValue), ScanMode::GreaterThan);
            case ScanMode::LessThan:
                return CompareNumeric(curr, std::get<T>(config.targetValue), ScanMode::LessThan);
            case ScanMode::Between:
                return CompareNumeric(curr, std::get<T>(config.targetValue),
                                      ScanMode::Between, std::get<T>(config.targetValue2));
            case ScanMode::Increased:
                return curr > prev;
            case ScanMode::Decreased:
                return curr < prev;
            case ScanMode::Changed:
                return curr != prev;
            case ScanMode::Unchanged:
                return curr == prev;
            case ScanMode::IncreasedBy: {
                T delta = std::get<T>(config.targetValue);
                T diff = curr - prev;
                if constexpr (std::is_floating_point_v<T>)
                    return std::abs(diff - delta) < T(0.001);
                else
                    return diff == delta;
            }
            case ScanMode::DecreasedBy: {
                T delta = std::get<T>(config.targetValue);
                T diff = prev - curr;
                if constexpr (std::is_floating_point_v<T>)
                    return std::abs(diff - delta) < T(0.001);
                else
                    return diff == delta;
            }
            default:
                return false;
        }
    };

    return std::visit([&](auto& curr) -> bool {
        using T = std::decay_t<decltype(curr)>;
        if constexpr (std::is_arithmetic_v<T>) {
            return doCompare(curr, std::get<T>(previous));
        }
        return false;
    }, current);
}

// ─── Numeric comparison ──────────────────────────────────

template<typename T>
bool MemoryScanner::CompareNumeric(T current, T target, ScanMode mode, T target2) const {
    if constexpr (std::is_floating_point_v<T>) {
        switch (mode) {
            case ScanMode::ExactValue:    return std::abs(current - target) < T(0.001);
            case ScanMode::GreaterThan:   return current > target;
            case ScanMode::LessThan:      return current < target;
            case ScanMode::Between:       return current >= target && current <= target2;
            default:                      return false;
        }
    } else {
        switch (mode) {
            case ScanMode::ExactValue:    return current == target;
            case ScanMode::GreaterThan:   return current > target;
            case ScanMode::LessThan:      return current < target;
            case ScanMode::Between:       return current >= target && current <= target2;
            default:                      return false;
        }
    }
}

// ─── First scan ──────────────────────────────────────────

bool MemoryScanner::FirstScan(const ScanConfig& config, ScanProgressCallback progressCb) {
    if (!m_hProcess || m_scanning.load()) return false;

    m_scanning.store(true);
    m_cancelRequested.store(false);

    {
        std::lock_guard<std::mutex> lock(m_resultsMutex);
        m_results.clear();
    }

    auto regions = GetMemoryRegions();

    // Filter regions based on config
    std::vector<MemoryRegion> filteredRegions;
    for (auto& r : regions) {
        if (r.baseAddress < config.startAddress) continue;
        if (r.baseAddress > config.endAddress) continue;

        bool isWritable = (r.protect & PAGE_READWRITE) ||
                         (r.protect & PAGE_WRITECOPY) ||
                         (r.protect & PAGE_EXECUTE_READWRITE) ||
                         (r.protect & PAGE_EXECUTE_WRITECOPY);

        if (config.writable && !isWritable) continue;

        filteredRegions.push_back(r);
    }

    // Calculate total bytes for progress
    SIZE_T totalBytes = 0;
    for (auto& r : filteredRegions) totalBytes += r.size;

    SIZE_T scannedBytes = 0;
    std::vector<ScanResult> allResults;

    // Use multiple threads for large scans
    unsigned int numThreads = std::min((unsigned int)filteredRegions.size(),
                                       std::thread::hardware_concurrency());
    if (numThreads == 0) numThreads = 1;

    if (numThreads == 1 || filteredRegions.size() < 4) {
        // Single-threaded for small scans
        for (size_t i = 0; i < filteredRegions.size(); i++) {
            if (m_cancelRequested.load()) break;

            ScanRegion(filteredRegions[i], config, allResults);
            scannedBytes += filteredRegions[i].size;

            if (progressCb) {
                float progress = totalBytes > 0
                    ? static_cast<float>(scannedBytes) / totalBytes
                    : 0.0f;
                progressCb(progress, allResults.size());
            }
        }
    } else {
        // Multi-threaded scan
        std::vector<std::thread> threads;
        std::vector<std::vector<ScanResult>> threadResults(numThreads);
        std::atomic<size_t> regionIndex{0};
        std::atomic<SIZE_T> scannedBytesAtomic{0};

        for (unsigned int t = 0; t < numThreads; t++) {
            threads.emplace_back([&, t]() {
                size_t idx;
                while ((idx = regionIndex.fetch_add(1)) < filteredRegions.size()) {
                    if (m_cancelRequested.load()) return;
                    ScanRegion(filteredRegions[idx], config, threadResults[t]);
                    scannedBytesAtomic.fetch_add(filteredRegions[idx].size);
                }
            });
        }

        // Progress reporting while threads work
        while (true) {
            bool allDone = true;
            for (auto& t : threads) {
                if (t.joinable()) {
                    allDone = false;
                    break;
                }
            }

            SIZE_T current = scannedBytesAtomic.load();
            size_t totalFound = 0;
            for (auto& tr : threadResults) totalFound += tr.size();

            if (progressCb) {
                float progress = totalBytes > 0
                    ? static_cast<float>(current) / totalBytes
                    : 0.0f;
                progressCb(progress, totalFound);
            }

            if (regionIndex.load() >= filteredRegions.size()) break;
            Sleep(50);
        }

        for (auto& t : threads) {
            if (t.joinable()) t.join();
        }

        // Merge results
        for (auto& tr : threadResults) {
            allResults.insert(allResults.end(),
                            std::make_move_iterator(tr.begin()),
                            std::make_move_iterator(tr.end()));
        }
    }

    // Sort results by address
    std::sort(allResults.begin(), allResults.end(),
              [](const ScanResult& a, const ScanResult& b) {
                  return a.address < b.address;
              });

    {
        std::lock_guard<std::mutex> lock(m_resultsMutex);
        m_results = std::move(allResults);
    }

    if (progressCb) {
        progressCb(1.0f, m_results.size());
    }

    m_scanning.store(false);
    return true;
}

// ─── Next scan (filter existing results) ─────────────────

bool MemoryScanner::NextScan(const ScanConfig& config, ScanProgressCallback progressCb) {
    if (!m_hProcess || m_scanning.load()) return false;
    if (m_results.empty()) return false;

    m_scanning.store(true);
    m_cancelRequested.store(false);

    std::vector<ScanResult> newResults;
    size_t total = m_results.size();

    for (size_t i = 0; i < total; i++) {
        if (m_cancelRequested.load()) break;

        ScanValue newValue = ReadValue(m_results[i].address, config.valueType);
        ScanValue prevValue = m_results[i].currentValue;

        bool matches = false;

        if (config.scanMode == ScanMode::Increased ||
            config.scanMode == ScanMode::Decreased ||
            config.scanMode == ScanMode::Changed ||
            config.scanMode == ScanMode::Unchanged ||
            config.scanMode == ScanMode::IncreasedBy ||
            config.scanMode == ScanMode::DecreasedBy) {
            matches = CompareValues(newValue, prevValue, config);
        } else {
            // For exact/range comparisons, check against target
            matches = CompareValues(newValue, newValue, config);
        }

        if (matches) {
            ScanResult result;
            result.address = m_results[i].address;
            result.currentValue = newValue;
            result.previousValue = prevValue;
            newResults.push_back(result);
        }

        if (progressCb && (i % 1000 == 0 || i == total - 1)) {
            progressCb(static_cast<float>(i + 1) / total, newResults.size());
        }
    }

    {
        std::lock_guard<std::mutex> lock(m_resultsMutex);
        m_results = std::move(newResults);
    }

    if (progressCb) {
        progressCb(1.0f, m_results.size());
    }

    m_scanning.store(false);
    return true;
}

// ─── Value <-> String conversion ─────────────────────────

std::string MemoryScanner::ValueToString(const ScanValue& val, ValueType type) {
    std::ostringstream oss;
    switch (type) {
        case ValueType::Int8:   oss << (int)std::get<int8_t>(val); break;
        case ValueType::Int16:  oss << std::get<int16_t>(val); break;
        case ValueType::Int32:  oss << std::get<int32_t>(val); break;
        case ValueType::Int64:  oss << std::get<int64_t>(val); break;
        case ValueType::Float:  oss << std::fixed << std::setprecision(3)
                                    << std::get<float>(val); break;
        case ValueType::Double: oss << std::fixed << std::setprecision(6)
                                    << std::get<double>(val); break;
        default:                oss << "?"; break;
    }
    return oss.str();
}

ScanValue MemoryScanner::StringToValue(const std::string& str, ValueType type) {
    try {
        switch (type) {
            case ValueType::Int8:   return static_cast<int8_t>(std::stoi(str));
            case ValueType::Int16:  return static_cast<int16_t>(std::stoi(str));
            case ValueType::Int32:  return static_cast<int32_t>(std::stol(str));
            case ValueType::Int64:  return static_cast<int64_t>(std::stoll(str));
            case ValueType::Float:  return std::stof(str);
            case ValueType::Double: return std::stod(str);
            default:                return int32_t(0);
        }
    } catch (...) {
        switch (type) {
            case ValueType::Int8:   return int8_t(0);
            case ValueType::Int16:  return int16_t(0);
            case ValueType::Int32:  return int32_t(0);
            case ValueType::Int64:  return int64_t(0);
            case ValueType::Float:  return float(0);
            case ValueType::Double: return double(0);
            default:                return int32_t(0);
        }
    }
}

} // namespace memforge
