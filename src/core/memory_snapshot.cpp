#include "core/memory_snapshot.h"
#include <cstring>
#include <cmath>
#include <sstream>
#include <iomanip>

namespace memforge {

bool MemorySnapshot::Capture(HANDLE hProcess) {
    m_regions.clear();
    m_valid = false;

    if (!hProcess) return false;

    MEMORY_BASIC_INFORMATION mbi{};
    uintptr_t address = 0;

    while (VirtualQueryEx(hProcess, reinterpret_cast<LPCVOID>(address), &mbi, sizeof(mbi))) {
        if (mbi.State == MEM_COMMIT &&
            (mbi.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_WRITECOPY)) &&
            !(mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS))) {

            std::vector<uint8_t> buffer(mbi.RegionSize);
            SIZE_T bytesRead = 0;
            if (ReadProcessMemory(hProcess, mbi.BaseAddress, buffer.data(),
                                  mbi.RegionSize, &bytesRead) && bytesRead > 0) {
                // Skip regions that are all zeros
                bool allZero = true;
                for (size_t i = 0; i < bytesRead; i += 64) {
                    if (buffer[i] != 0) { allZero = false; break; }
                }
                if (!allZero) {
                    buffer.resize(bytesRead);
                    Region region;
                    region.base = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
                    region.data = std::move(buffer);
                    m_regions.push_back(std::move(region));
                }
            }
        }

        uintptr_t next = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
        if (next <= address) break; // overflow protection
        address = next;
    }

    m_valid = !m_regions.empty();
    return m_valid;
}

std::vector<MemoryDiff> MemorySnapshot::Compare(const MemorySnapshot& other) const {
    std::vector<MemoryDiff> diffs;

    // For each region in 'this', find matching region in 'other'
    for (const auto& regionA : m_regions) {
        for (const auto& regionB : other.m_regions) {
            // Check for overlap
            uintptr_t startA = regionA.base;
            uintptr_t endA = startA + regionA.data.size();
            uintptr_t startB = regionB.base;
            uintptr_t endB = startB + regionB.data.size();

            uintptr_t overlapStart = (std::max)(startA, startB);
            uintptr_t overlapEnd = (std::min)(endA, endB);

            if (overlapStart >= overlapEnd) continue;

            // Compare 4 bytes at a time (aligned)
            uintptr_t alignedStart = (overlapStart + 3) & ~static_cast<uintptr_t>(3);

            for (uintptr_t addr = alignedStart; addr + 4 <= overlapEnd; addr += 4) {
                size_t offA = addr - startA;
                size_t offB = addr - startB;

                const uint8_t* bytesA = regionA.data.data() + offA;
                const uint8_t* bytesB = regionB.data.data() + offB;

                if (std::memcmp(bytesA, bytesB, 4) != 0) {
                    MemoryDiff diff;
                    diff.address = addr;
                    diff.before.assign(bytesA, bytesA + 4);
                    diff.after.assign(bytesB, bytesB + 4);
                    diff.guessedType = GuessType(bytesA, bytesB);
                    diff.beforeStr = FormatValue(bytesA, diff.guessedType);
                    diff.afterStr = FormatValue(bytesB, diff.guessedType);
                    diffs.push_back(std::move(diff));

                    if (diffs.size() >= 500000) return diffs; // cap
                }
            }
        }
    }

    return diffs;
}

std::vector<MemoryDiff> MemorySnapshot::FilterIncreased(const std::vector<MemoryDiff>& diffs) {
    std::vector<MemoryDiff> result;
    for (const auto& d : diffs) {
        if (d.before.size() >= 4 && d.after.size() >= 4) {
            int32_t beforeVal, afterVal;
            std::memcpy(&beforeVal, d.before.data(), 4);
            std::memcpy(&afterVal, d.after.data(), 4);
            if (afterVal > beforeVal) {
                result.push_back(d);
            }
        }
    }
    return result;
}

std::vector<MemoryDiff> MemorySnapshot::FilterDecreased(const std::vector<MemoryDiff>& diffs) {
    std::vector<MemoryDiff> result;
    for (const auto& d : diffs) {
        if (d.before.size() >= 4 && d.after.size() >= 4) {
            int32_t beforeVal, afterVal;
            std::memcpy(&beforeVal, d.before.data(), 4);
            std::memcpy(&afterVal, d.after.data(), 4);
            if (afterVal < beforeVal) {
                result.push_back(d);
            }
        }
    }
    return result;
}

size_t MemorySnapshot::GetTotalSize() const {
    size_t total = 0;
    for (const auto& r : m_regions) {
        total += r.data.size();
    }
    return total;
}

std::string MemorySnapshot::GuessType(const uint8_t* beforeBytes, const uint8_t* afterBytes) {
    float fBefore, fAfter;
    std::memcpy(&fBefore, beforeBytes, 4);
    std::memcpy(&fAfter, afterBytes, 4);

    // Check if both look like reasonable floats
    if (std::isfinite(fBefore) && std::isfinite(fAfter)) {
        float absBefore = std::fabs(fBefore);
        float absAfter = std::fabs(fAfter);
        if (absBefore > 0.001f && absBefore < 100000.0f &&
            absAfter > 0.001f && absAfter < 100000.0f) {
            return "Float";
        }
    }

    // Check if value looks like a pointer (>0x10000)
    uint32_t val;
    std::memcpy(&val, afterBytes, 4);
    if (val > 0x10000) {
        return "Pointer";
    }

    return "Int32";
}

std::string MemorySnapshot::FormatValue(const uint8_t* bytes, const std::string& type) {
    std::ostringstream oss;
    if (type == "Float") {
        float f;
        std::memcpy(&f, bytes, 4);
        oss << std::fixed << std::setprecision(3) << f;
    } else if (type == "Pointer") {
        uint32_t val;
        std::memcpy(&val, bytes, 4);
        oss << "0x" << std::hex << std::uppercase << val;
    } else {
        int32_t val;
        std::memcpy(&val, bytes, 4);
        oss << val;
    }
    return oss.str();
}

} // namespace memforge
