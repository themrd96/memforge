#include "core/aob_scanner.h"
#include <Windows.h>
#include <TlHelp32.h>
#include <sstream>
#include <thread>
#include <mutex>
#include <algorithm>

namespace memforge {

bool AobScanner::ParsePattern(const std::string& patternStr,
                              std::vector<uint8_t>& bytes,
                              std::vector<bool>& mask) {
    bytes.clear();
    mask.clear();

    std::istringstream iss(patternStr);
    std::string token;

    while (iss >> token) {
        if (token == "??" || token == "?") {
            bytes.push_back(0x00);
            mask.push_back(false);
        } else {
            // Parse hex byte
            unsigned long val = 0;
            try {
                val = std::stoul(token, nullptr, 16);
            } catch (...) {
                return false;
            }
            if (val > 0xFF) return false;
            bytes.push_back(static_cast<uint8_t>(val));
            mask.push_back(true);
        }
    }

    return !bytes.empty();
}

bool AobScanner::Scan(HANDLE hProcess, const std::string& pattern,
                      std::function<void(float, size_t)> progressCb) {
    if (m_scanning.load()) return false;

    std::vector<uint8_t> patternBytes;
    std::vector<bool> patternMask;
    if (!ParsePattern(pattern, patternBytes, patternMask)) return false;

    m_scanning.store(true);
    m_cancelRequested.store(false);
    m_results.clear();
    m_originalBytes.clear();

    // Enumerate memory regions
    struct Region {
        uintptr_t base;
        SIZE_T size;
    };
    std::vector<Region> regions;

    MEMORY_BASIC_INFORMATION mbi = {};
    uintptr_t addr = 0;
    while (VirtualQueryEx(hProcess, reinterpret_cast<LPCVOID>(addr), &mbi, sizeof(mbi))) {
        if (mbi.State == MEM_COMMIT &&
            (mbi.Protect & (PAGE_READWRITE | PAGE_READONLY | PAGE_EXECUTE_READ |
                            PAGE_EXECUTE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_WRITECOPY)) &&
            !(mbi.Protect & PAGE_GUARD)) {
            regions.push_back({reinterpret_cast<uintptr_t>(mbi.BaseAddress), mbi.RegionSize});
        }
        addr = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
        if (addr == 0) break; // overflow
    }

    // Multi-threaded scan
    std::mutex resultsMutex;
    std::atomic<size_t> regionsScanned{0};
    size_t totalRegions = regions.size();

    auto scanWorker = [&](size_t startIdx, size_t endIdx) {
        for (size_t ri = startIdx; ri < endIdx; ri++) {
            if (m_cancelRequested.load()) break;

            auto& region = regions[ri];
            std::vector<uint8_t> buffer(region.size);
            SIZE_T bytesRead = 0;
            if (!ReadProcessMemory(hProcess, reinterpret_cast<LPCVOID>(region.base),
                                   buffer.data(), region.size, &bytesRead)) {
                regionsScanned.fetch_add(1);
                continue;
            }

            size_t patLen = patternBytes.size();
            if (bytesRead < patLen) {
                regionsScanned.fetch_add(1);
                continue;
            }

            for (size_t i = 0; i <= bytesRead - patLen; i++) {
                if (m_cancelRequested.load()) break;

                bool match = true;
                for (size_t j = 0; j < patLen; j++) {
                    if (patternMask[j] && buffer[i + j] != patternBytes[j]) {
                        match = false;
                        break;
                    }
                }

                if (match) {
                    AobResult result;
                    result.address = region.base + i;
                    result.matchedBytes.assign(buffer.data() + i, buffer.data() + i + patLen);

                    std::lock_guard<std::mutex> lock(resultsMutex);
                    m_results.push_back(result);
                    m_originalBytes[result.address] = result.matchedBytes;
                }
            }

            regionsScanned.fetch_add(1);
            if (progressCb) {
                float progress = static_cast<float>(regionsScanned.load()) / static_cast<float>(totalRegions);
                size_t found;
                {
                    std::lock_guard<std::mutex> lock(resultsMutex);
                    found = m_results.size();
                }
                progressCb(progress, found);
            }
        }
    };

    unsigned int threadCount = std::thread::hardware_concurrency();
    if (threadCount == 0) threadCount = 4;
    if (threadCount > totalRegions) threadCount = static_cast<unsigned int>(totalRegions);
    if (threadCount == 0) threadCount = 1;

    size_t perThread = totalRegions / threadCount;
    std::vector<std::thread> threads;
    for (unsigned int t = 0; t < threadCount; t++) {
        size_t startIdx = t * perThread;
        size_t endIdx = (t == threadCount - 1) ? totalRegions : (t + 1) * perThread;
        threads.emplace_back(scanWorker, startIdx, endIdx);
    }

    for (auto& th : threads) {
        th.join();
    }

    m_scanning.store(false);
    if (progressCb) progressCb(1.0f, m_results.size());
    return true;
}

bool AobScanner::ScanModule(HANDLE hProcess, const std::string& moduleName,
                            const std::string& pattern) {
    if (m_scanning.load()) return false;

    std::vector<uint8_t> patternBytes;
    std::vector<bool> patternMask;
    if (!ParsePattern(pattern, patternBytes, patternMask)) return false;

    m_scanning.store(true);
    m_cancelRequested.store(false);
    m_results.clear();
    m_originalBytes.clear();

    // Find module base and size
    DWORD pid = GetProcessId(hProcess);
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snap == INVALID_HANDLE_VALUE) {
        m_scanning.store(false);
        return false;
    }

    MODULEENTRY32W me = {};
    me.dwSize = sizeof(me);

    uintptr_t moduleBase = 0;
    SIZE_T moduleSize = 0;

    if (Module32FirstW(snap, &me)) {
        do {
            // Convert wide module name to narrow for comparison
            char narrowName[MAX_PATH] = {};
            WideCharToMultiByte(CP_UTF8, 0, me.szModule, -1, narrowName, MAX_PATH, nullptr, nullptr);
            if (_stricmp(narrowName, moduleName.c_str()) == 0) {
                moduleBase = reinterpret_cast<uintptr_t>(me.modBaseAddr);
                moduleSize = me.modBaseSize;
                break;
            }
        } while (Module32NextW(snap, &me));
    }
    CloseHandle(snap);

    if (moduleBase == 0) {
        m_scanning.store(false);
        return false;
    }

    // Read module memory
    std::vector<uint8_t> buffer(moduleSize);
    SIZE_T bytesRead = 0;
    ReadProcessMemory(hProcess, reinterpret_cast<LPCVOID>(moduleBase),
                      buffer.data(), moduleSize, &bytesRead);

    size_t patLen = patternBytes.size();
    if (bytesRead >= patLen) {
        for (size_t i = 0; i <= bytesRead - patLen; i++) {
            if (m_cancelRequested.load()) break;

            bool match = true;
            for (size_t j = 0; j < patLen; j++) {
                if (patternMask[j] && buffer[i + j] != patternBytes[j]) {
                    match = false;
                    break;
                }
            }

            if (match) {
                AobResult result;
                result.address = moduleBase + i;
                result.matchedBytes.assign(buffer.data() + i, buffer.data() + i + patLen);
                m_results.push_back(result);
                m_originalBytes[result.address] = result.matchedBytes;
            }
        }
    }

    m_scanning.store(false);
    return true;
}

void AobScanner::Cancel() {
    m_cancelRequested.store(true);
}

bool AobScanner::IsScanning() const {
    return m_scanning.load();
}

const std::vector<AobResult>& AobScanner::GetResults() const {
    return m_results;
}

void AobScanner::Reset() {
    m_results.clear();
    m_originalBytes.clear();
}

bool AobScanner::NopAt(HANDLE hProcess, uintptr_t address, size_t count) {
    DWORD oldProtect = 0;
    if (!VirtualProtectEx(hProcess, reinterpret_cast<LPVOID>(address), count,
                          PAGE_EXECUTE_READWRITE, &oldProtect)) {
        return false;
    }

    std::vector<uint8_t> nops(count, 0x90);
    SIZE_T written = 0;
    bool ok = WriteProcessMemory(hProcess, reinterpret_cast<LPVOID>(address),
                                 nops.data(), count, &written) != 0;

    VirtualProtectEx(hProcess, reinterpret_cast<LPVOID>(address), count,
                     oldProtect, &oldProtect);
    return ok && written == count;
}

bool AobScanner::RestoreAt(HANDLE hProcess, uintptr_t address) {
    auto it = m_originalBytes.find(address);
    if (it == m_originalBytes.end()) return false;

    const auto& original = it->second;
    DWORD oldProtect = 0;
    if (!VirtualProtectEx(hProcess, reinterpret_cast<LPVOID>(address), original.size(),
                          PAGE_EXECUTE_READWRITE, &oldProtect)) {
        return false;
    }

    SIZE_T written = 0;
    bool ok = WriteProcessMemory(hProcess, reinterpret_cast<LPVOID>(address),
                                 original.data(), original.size(), &written) != 0;

    VirtualProtectEx(hProcess, reinterpret_cast<LPVOID>(address), original.size(),
                     oldProtect, &oldProtect);
    return ok;
}

} // namespace memforge
