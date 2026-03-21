#include "core/pointer_scanner.h"
#include "core/process_manager.h"
#include <sstream>
#include <iomanip>
#include <cstring>
#include <unordered_map>
#include <queue>
#include <algorithm>

namespace memforge {

// ── PointerPath ──────────────────────────────────────────

uintptr_t PointerPath::Resolve(HANDLE hProcess) const {
    uintptr_t addr = baseAddress;

    for (size_t i = 0; i < offsets.size(); i++) {
        // Read pointer at current address
        uintptr_t val = 0;
        SIZE_T bytesRead = 0;
        if (!ReadProcessMemory(hProcess, reinterpret_cast<LPCVOID>(addr),
                               &val, sizeof(val), &bytesRead) || bytesRead != sizeof(val)) {
            return 0;
        }
        // Apply offset
        if (i + 1 < offsets.size()) {
            addr = val + offsets[i];
        } else {
            addr = val + offsets[i];
        }
    }

    return addr;
}

std::string PointerPath::ToString() const {
    std::ostringstream ss;
    ss << moduleName << "+0x" << std::hex << std::uppercase << baseAddress;
    for (auto off : offsets) {
        ss << " -> 0x" << std::hex << off;
    }
    return ss.str();
}

// ── PointerScanner ───────────────────────────────────────

PointerScanner::~PointerScanner() {
    CancelScan();
    if (m_scanThread.joinable()) {
        m_scanThread.join();
    }
}

bool PointerScanner::StartScan(HANDLE hProcess, DWORD pid, const PointerScanConfig& config,
                                std::function<void(float, size_t)> progressCb) {
    if (m_scanning.load()) return false;

    // Wait for previous thread
    if (m_scanThread.joinable()) {
        m_scanThread.join();
    }

    m_cancelRequested.store(false);
    m_scanning.store(true);

    {
        std::lock_guard<std::mutex> lock(m_resultsMutex);
        m_results.clear();
    }

    m_scanThread = std::thread(&PointerScanner::DoScan, this, hProcess, pid, config, progressCb);
    return true;
}

void PointerScanner::CancelScan() {
    m_cancelRequested.store(true);
}

bool PointerScanner::Rescan(HANDLE hProcess, uintptr_t newTargetAddress) {
    std::lock_guard<std::mutex> lock(m_resultsMutex);

    std::vector<PointerPath> valid;
    for (auto& path : m_results) {
        uintptr_t resolved = path.Resolve(hProcess);
        if (resolved == newTargetAddress) {
            valid.push_back(path);
        }
    }
    m_results = std::move(valid);
    return !m_results.empty();
}

void PointerScanner::DoScan(HANDLE hProcess, DWORD pid, PointerScanConfig config,
                              std::function<void(float, size_t)> progressCb) {

    // Step 1: Get module bases as potential static anchors
    auto modules = ProcessManager::GetModules(pid);
    if (modules.empty()) {
        m_scanning.store(false);
        return;
    }

    // Step 2: Enumerate memory regions and build a reverse pointer map
    // For each address that contains a value looking like a pointer,
    // record: destination -> list of source addresses
    struct PointerEntry {
        uintptr_t sourceAddr;
    };

    std::unordered_map<uintptr_t, std::vector<PointerEntry>> reverseMap;

    MEMORY_BASIC_INFORMATION mbi = {};
    uintptr_t addr = 0;
    size_t totalRegions = 0;
    size_t processedRegions = 0;

    // First pass: count regions
    uintptr_t countAddr = 0;
    while (VirtualQueryEx(hProcess, reinterpret_cast<LPCVOID>(countAddr), &mbi, sizeof(mbi))) {
        if (mbi.State == MEM_COMMIT && (mbi.Protect & PAGE_READWRITE ||
            mbi.Protect & PAGE_READONLY || mbi.Protect & PAGE_EXECUTE_READ ||
            mbi.Protect & PAGE_EXECUTE_READWRITE)) {
            totalRegions++;
        }
        countAddr = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
        if (countAddr < reinterpret_cast<uintptr_t>(mbi.BaseAddress)) break; // overflow
    }

    if (totalRegions == 0) {
        m_scanning.store(false);
        return;
    }

    // Second pass: read and build reverse map
    addr = 0;
    while (VirtualQueryEx(hProcess, reinterpret_cast<LPCVOID>(addr), &mbi, sizeof(mbi))) {
        if (m_cancelRequested.load()) break;

        if (mbi.State == MEM_COMMIT && (mbi.Protect & PAGE_READWRITE ||
            mbi.Protect & PAGE_READONLY || mbi.Protect & PAGE_EXECUTE_READ ||
            mbi.Protect & PAGE_EXECUTE_READWRITE)) {

            uintptr_t regionBase = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
            SIZE_T regionSize = mbi.RegionSize;

            // Cap region read to 16MB to avoid huge allocations
            if (regionSize > 16 * 1024 * 1024) {
                regionSize = 16 * 1024 * 1024;
            }

            std::vector<uint8_t> buffer(regionSize);
            SIZE_T bytesRead = 0;
            if (ReadProcessMemory(hProcess, mbi.BaseAddress, buffer.data(),
                                  regionSize, &bytesRead) && bytesRead >= sizeof(uintptr_t)) {

                // Scan for pointer-sized values that look like valid addresses
                for (size_t i = 0; i + sizeof(uintptr_t) <= bytesRead; i += sizeof(uintptr_t)) {
                    uintptr_t val;
                    std::memcpy(&val, buffer.data() + i, sizeof(uintptr_t));

                    // Check if val looks like a valid user-space address
                    if (val > 0x10000 && val < 0x00007FFFFFFFFFFF) {
                        uintptr_t sourceAddr = regionBase + i;
                        // Align target to page boundaries for the map key
                        // to reduce map size - use aligned address
                        uintptr_t aligned = val & ~0xFFF;
                        (void)aligned;
                        reverseMap[val].push_back({sourceAddr});
                    }
                }
            }

            processedRegions++;
            if (progressCb) {
                progressCb(static_cast<float>(processedRegions) / totalRegions * 0.7f, 0);
            }
        }

        addr = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
        if (addr < reinterpret_cast<uintptr_t>(mbi.BaseAddress)) break;
    }

    if (m_cancelRequested.load()) {
        m_scanning.store(false);
        return;
    }

    // Step 3: BFS from target address back to module bases
    // Build a set of static base addresses from modules
    struct BfsNode {
        uintptr_t address;
        std::vector<int64_t> offsetChain;
    };

    std::queue<BfsNode> bfsQueue;
    std::vector<PointerPath> localResults;

    // Start BFS from addresses that point near the target
    for (int64_t off = 0; off <= config.maxOffset; off += 4) {
        uintptr_t lookFor = config.targetAddress - off;
        auto it = reverseMap.find(lookFor);
        if (it != reverseMap.end()) {
            for (auto& entry : it->second) {
                BfsNode node;
                node.address = entry.sourceAddr;
                node.offsetChain.push_back(off);
                bfsQueue.push(node);
            }
        }
        if (off > 0) {
            lookFor = config.targetAddress + off;
            it = reverseMap.find(lookFor);
            if (it != reverseMap.end()) {
                for (auto& entry : it->second) {
                    BfsNode node;
                    node.address = entry.sourceAddr;
                    node.offsetChain.push_back(-off);
                    bfsQueue.push(node);
                }
            }
        }
    }

    size_t maxResults = 1000;
    size_t bfsIter = 0;

    while (!bfsQueue.empty() && !m_cancelRequested.load() && localResults.size() < maxResults) {
        BfsNode current = bfsQueue.front();
        bfsQueue.pop();
        bfsIter++;

        if (progressCb && bfsIter % 100 == 0) {
            progressCb(0.7f + 0.3f * (std::min)(1.0f, static_cast<float>(localResults.size()) / 100.0f),
                       localResults.size());
        }

        // Check if this address is within a module base
        for (auto& mod : modules) {
            if (current.address >= mod.baseAddress &&
                current.address < mod.baseAddress + mod.size) {
                PointerPath path;
                path.baseAddress = current.address - mod.baseAddress;
                path.moduleName = mod.name;
                path.offsets = current.offsetChain;
                localResults.push_back(path);
                break;
            }
        }

        // Continue BFS if not too deep
        if (static_cast<int>(current.offsetChain.size()) < config.maxLevel) {
            for (int64_t off = 0; off <= config.maxOffset; off += sizeof(uintptr_t)) {
                uintptr_t lookFor = current.address - off;
                auto it = reverseMap.find(lookFor);
                if (it != reverseMap.end()) {
                    for (auto& entry : it->second) {
                        BfsNode next;
                        next.address = entry.sourceAddr;
                        next.offsetChain = current.offsetChain;
                        next.offsetChain.push_back(off);
                        bfsQueue.push(next);
                    }
                }
                if (bfsQueue.size() > 100000) break; // prevent memory explosion
            }
        }
    }

    {
        std::lock_guard<std::mutex> lock(m_resultsMutex);
        m_results = std::move(localResults);
    }

    if (progressCb) {
        progressCb(1.0f, m_results.size());
    }

    m_scanning.store(false);
}

} // namespace memforge
