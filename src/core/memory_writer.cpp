#include "core/memory_writer.h"

namespace memforge {

bool MemoryWriter::WriteValue(uintptr_t address, const ScanValue& value, ValueType type) {
    size_t size = MemoryScanner::GetValueSize(type);
    uint8_t buffer[8] = {};

    switch (type) {
        case ValueType::Int8:   *reinterpret_cast<int8_t*>(buffer) = std::get<int8_t>(value); break;
        case ValueType::Int16:  *reinterpret_cast<int16_t*>(buffer) = std::get<int16_t>(value); break;
        case ValueType::Int32:  *reinterpret_cast<int32_t*>(buffer) = std::get<int32_t>(value); break;
        case ValueType::Int64:  *reinterpret_cast<int64_t*>(buffer) = std::get<int64_t>(value); break;
        case ValueType::Float:  *reinterpret_cast<float*>(buffer) = std::get<float>(value); break;
        case ValueType::Double: *reinterpret_cast<double*>(buffer) = std::get<double>(value); break;
        default: return false;
    }

    return WriteProtected(address, buffer, size);
}

bool MemoryWriter::WriteBytes(uintptr_t address, const std::vector<uint8_t>& data) {
    return WriteProtected(address, data.data(), data.size());
}

std::vector<uint8_t> MemoryWriter::ReadBytes(uintptr_t address, size_t size) {
    std::vector<uint8_t> data(size);
    SIZE_T bytesRead = 0;
    if (!ReadProcessMemory(m_hProcess, reinterpret_cast<LPCVOID>(address),
                           data.data(), size, &bytesRead)) {
        data.resize(bytesRead);
    }
    return data;
}

bool MemoryWriter::NopBytes(uintptr_t address, size_t count) {
    std::vector<uint8_t> nops(count, 0x90);
    return WriteProtected(address, nops.data(), count);
}

bool MemoryWriter::WriteProtected(uintptr_t address, const void* data, size_t size) {
    if (!m_hProcess) return false;

    DWORD oldProtect = 0;
    // Change protection to allow writing
    if (!VirtualProtectEx(m_hProcess, reinterpret_cast<LPVOID>(address),
                          size, PAGE_EXECUTE_READWRITE, &oldProtect)) {
        // Try writing anyway - some regions are already writable
    }

    SIZE_T written = 0;
    bool success = WriteProcessMemory(m_hProcess, reinterpret_cast<LPVOID>(address),
                                       data, size, &written) != FALSE;

    // Restore original protection
    if (oldProtect != 0) {
        DWORD temp = 0;
        VirtualProtectEx(m_hProcess, reinterpret_cast<LPVOID>(address),
                        size, oldProtect, &temp);
    }

    return success && written == size;
}

} // namespace memforge
