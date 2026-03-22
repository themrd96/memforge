#pragma once
#include <Windows.h>
#include <cstdint>
#include <string>
#include <vector>
#include "core/memory_scanner.h"

namespace memforge {

// Extended value types for structure dissection
enum class FieldType {
    Int8,
    Int16,
    Int32,
    Int64,
    UInt8,
    UInt16,
    UInt32,
    UInt64,
    Float,
    Double,
    Pointer,
    String,
    Padding
};

struct StructField {
    std::string name;
    FieldType type;
    size_t offset;
    size_t size;        // auto-calculated from type
    std::string comment;
};

struct StructDefinition {
    std::string name;
    uintptr_t baseAddress = 0;
    std::vector<StructField> fields;

    // Generate C++ struct code
    std::string GenerateCppStruct() const;

    // Auto-detect types by heuristic
    static FieldType GuessType(const uint8_t* data, size_t maxLen);

    // Get size of a field type in bytes
    static size_t GetFieldSize(FieldType type);

    // Get display name for a field type
    static const char* GetFieldTypeName(FieldType type);

    // Get total struct size
    size_t GetTotalSize() const;

    // Sort fields by offset
    void SortFields();

    // Add a field at the next available offset
    void AddField(const std::string& name, FieldType type, size_t offset);

    // Remove field by index
    void RemoveField(size_t index);
};

// One hit from a nearby search
struct NearbyResult {
    uintptr_t address;
    int       offsetFromBase; // signed — negative means before base address
    int32_t   asInt32;
    uint32_t  asUInt32;
    float     asFloat;
    int64_t   asInt64;
    double    asDouble;
    uint8_t   rawBytes[8];
};

class StructureDissector {
public:
    void SetProcess(HANDLE hProcess) { m_hProcess = hProcess; }

    // Read memory at base + offset
    bool ReadFieldValue(uintptr_t baseAddress, size_t offset, size_t size,
                        void* outBuffer) const;

    // Read raw bytes for hex display
    bool ReadBytes(uintptr_t address, size_t size, std::vector<uint8_t>& outBytes) const;

    // Format a field value as string
    std::string FormatFieldValue(uintptr_t baseAddress, const StructField& field) const;

    // Format field as hex bytes
    std::string FormatFieldHex(uintptr_t baseAddress, const StructField& field) const;

    // Auto-detect structure fields from memory
    StructDefinition AutoDetect(uintptr_t baseAddress, size_t totalSize) const;

    // Scan memory in [baseAddress - rangeBefore, baseAddress + rangeAfter],
    // stepping by `alignment` bytes (default 4). Returns every location
    // whose Int32 or Float value matches the optional filter.
    // Pass filterEnabled=false to return all locations.
    std::vector<NearbyResult> NearbySearch(
        uintptr_t baseAddress,
        int       rangeBefore,
        int       rangeAfter,
        int       alignment,
        bool      filterEnabled,
        float     filterValue,
        float     filterTolerance) const;

private:
    HANDLE m_hProcess = nullptr;
};

} // namespace memforge
