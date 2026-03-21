#pragma once
#include <Windows.h>
#include <cstdint>
#include <vector>
#include <string>

namespace memforge {

struct MemoryDiff {
    uintptr_t address;
    std::vector<uint8_t> before;
    std::vector<uint8_t> after;
    std::string guessedType; // "Int32", "Float", "Pointer"
    std::string beforeStr;   // human-readable
    std::string afterStr;
};

class MemorySnapshot {
public:
    // Take a snapshot of all readable writable committed memory
    bool Capture(HANDLE hProcess);

    // Compare this snapshot with another, return differences
    std::vector<MemoryDiff> Compare(const MemorySnapshot& other) const;

    // Filter diffs
    static std::vector<MemoryDiff> FilterIncreased(const std::vector<MemoryDiff>& diffs);
    static std::vector<MemoryDiff> FilterDecreased(const std::vector<MemoryDiff>& diffs);

    bool IsValid() const { return m_valid; }
    size_t GetTotalSize() const;
    size_t GetRegionCount() const { return m_regions.size(); }

private:
    struct Region {
        uintptr_t base;
        std::vector<uint8_t> data;
    };
    std::vector<Region> m_regions;
    bool m_valid = false;

    static std::string GuessType(const uint8_t* beforeBytes, const uint8_t* afterBytes);
    static std::string FormatValue(const uint8_t* bytes, const std::string& type);
};

} // namespace memforge
