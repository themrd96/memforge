#include "core/undo_history.h"
#include <cstring>
#include <sstream>
#include <iomanip>

namespace memforge {

void UndoHistory::RecordWrite(HANDLE hProcess, uintptr_t address,
                               const void* newData, size_t size,
                               const std::string& description) {
    if (!hProcess || !newData || size == 0) return;

    MemoryWrite entry;
    entry.id = m_nextId++;
    entry.address = address;
    entry.timestamp = GetTickCount64();
    entry.undone = false;

    // Read old value before overwriting
    entry.oldValue.resize(size);
    SIZE_T bytesRead = 0;
    ReadProcessMemory(hProcess, reinterpret_cast<LPCVOID>(address),
                      entry.oldValue.data(), size, &bytesRead);
    if (bytesRead < size) {
        entry.oldValue.resize(bytesRead);
    }

    // Store new value
    entry.newValue.resize(size);
    std::memcpy(entry.newValue.data(), newData, size);

    // Generate description if none provided
    if (description.empty()) {
        std::ostringstream oss;
        oss << "Write " << size << " bytes at 0x"
            << std::hex << std::uppercase << address;
        entry.description = oss.str();
    } else {
        entry.description = description;
    }

    m_history.push_back(std::move(entry));
}

bool UndoHistory::Undo(HANDLE hProcess) {
    if (!hProcess) return false;

    // Find the last non-undone write
    for (int i = static_cast<int>(m_history.size()) - 1; i >= 0; --i) {
        if (!m_history[i].undone) {
            return UndoById(hProcess, m_history[i].id);
        }
    }
    return false;
}

bool UndoHistory::UndoById(HANDLE hProcess, int id) {
    if (!hProcess) return false;

    for (auto& entry : m_history) {
        if (entry.id == id && !entry.undone) {
            // Restore old value
            DWORD oldProtect = 0;
            VirtualProtectEx(hProcess, reinterpret_cast<LPVOID>(entry.address),
                             entry.oldValue.size(), PAGE_EXECUTE_READWRITE, &oldProtect);

            SIZE_T bytesWritten = 0;
            BOOL ok = WriteProcessMemory(hProcess, reinterpret_cast<LPVOID>(entry.address),
                                         entry.oldValue.data(), entry.oldValue.size(),
                                         &bytesWritten);

            VirtualProtectEx(hProcess, reinterpret_cast<LPVOID>(entry.address),
                             entry.oldValue.size(), oldProtect, &oldProtect);

            if (ok) {
                entry.undone = true;
                return true;
            }
            return false;
        }
    }
    return false;
}

bool UndoHistory::Redo(HANDLE hProcess, int id) {
    if (!hProcess) return false;

    for (auto& entry : m_history) {
        if (entry.id == id && entry.undone) {
            // Re-apply new value
            DWORD oldProtect = 0;
            VirtualProtectEx(hProcess, reinterpret_cast<LPVOID>(entry.address),
                             entry.newValue.size(), PAGE_EXECUTE_READWRITE, &oldProtect);

            SIZE_T bytesWritten = 0;
            BOOL ok = WriteProcessMemory(hProcess, reinterpret_cast<LPVOID>(entry.address),
                                         entry.newValue.data(), entry.newValue.size(),
                                         &bytesWritten);

            VirtualProtectEx(hProcess, reinterpret_cast<LPVOID>(entry.address),
                             entry.newValue.size(), oldProtect, &oldProtect);

            if (ok) {
                entry.undone = false;
                return true;
            }
            return false;
        }
    }
    return false;
}

void UndoHistory::Clear() {
    m_history.clear();
    m_nextId = 1;
}

} // namespace memforge
