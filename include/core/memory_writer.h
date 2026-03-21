#pragma once
#include <Windows.h>
#include <cstdint>
#include <vector>
#include <string>
#include "core/memory_scanner.h"

namespace memforge {

class MemoryWriter {
public:
    MemoryWriter() = default;

    void Attach(HANDLE hProcess) { m_hProcess = hProcess; }
    void Detach() { m_hProcess = nullptr; }

    // Write a value to an address
    bool WriteValue(uintptr_t address, const ScanValue& value, ValueType type);

    // Write raw bytes
    bool WriteBytes(uintptr_t address, const std::vector<uint8_t>& data);

    // Read raw bytes
    std::vector<uint8_t> ReadBytes(uintptr_t address, size_t size);

    // NOP out instructions (fill with 0x90)
    bool NopBytes(uintptr_t address, size_t count);

    // Write with memory protection change (for code sections)
    bool WriteProtected(uintptr_t address, const void* data, size_t size);

    HANDLE GetProcessHandle() const { return m_hProcess; }

private:
    HANDLE m_hProcess = nullptr;
};

} // namespace memforge
