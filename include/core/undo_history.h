#pragma once
#include <Windows.h>
#include <cstdint>
#include <vector>
#include <string>

namespace memforge {

struct MemoryWrite {
    int id;
    uintptr_t address;
    std::vector<uint8_t> oldValue;
    std::vector<uint8_t> newValue;
    std::string description; // "Set 0x1A3400 to 999999"
    uint64_t timestamp;      // GetTickCount64()
    bool undone = false;
};

class UndoHistory {
public:
    // Record a write (call this before writing)
    void RecordWrite(HANDLE hProcess, uintptr_t address,
                     const void* newData, size_t size,
                     const std::string& description = "");

    // Undo the last write
    bool Undo(HANDLE hProcess);

    // Undo a specific write by id
    bool UndoById(HANDLE hProcess, int id);

    // Redo (re-apply an undone write)
    bool Redo(HANDLE hProcess, int id);

    // Get history
    const std::vector<MemoryWrite>& GetHistory() const { return m_history; }

    // Clear history
    void Clear();

    size_t GetCount() const { return m_history.size(); }

private:
    std::vector<MemoryWrite> m_history;
    int m_nextId = 1;
};

} // namespace memforge
