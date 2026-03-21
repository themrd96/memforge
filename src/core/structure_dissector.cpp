#include "core/structure_dissector.h"
#include <sstream>
#include <iomanip>
#include <cstring>
#include <cmath>
#include <algorithm>

namespace memforge {

// ── StructDefinition ─────────────────────────────────────

size_t StructDefinition::GetFieldSize(FieldType type) {
    switch (type) {
        case FieldType::Int8:    case FieldType::UInt8:   return 1;
        case FieldType::Int16:   case FieldType::UInt16:  return 2;
        case FieldType::Int32:   case FieldType::UInt32:  case FieldType::Float: return 4;
        case FieldType::Int64:   case FieldType::UInt64:  case FieldType::Double:
        case FieldType::Pointer: return 8;
        case FieldType::String:  return 64; // default string read size
        case FieldType::Padding: return 1;
    }
    return 4;
}

const char* StructDefinition::GetFieldTypeName(FieldType type) {
    switch (type) {
        case FieldType::Int8:    return "Int8";
        case FieldType::Int16:   return "Int16";
        case FieldType::Int32:   return "Int32";
        case FieldType::Int64:   return "Int64";
        case FieldType::UInt8:   return "UInt8";
        case FieldType::UInt16:  return "UInt16";
        case FieldType::UInt32:  return "UInt32";
        case FieldType::UInt64:  return "UInt64";
        case FieldType::Float:   return "Float";
        case FieldType::Double:  return "Double";
        case FieldType::Pointer: return "Pointer";
        case FieldType::String:  return "String";
        case FieldType::Padding: return "Padding";
    }
    return "Unknown";
}

size_t StructDefinition::GetTotalSize() const {
    size_t total = 0;
    for (auto& f : fields) {
        size_t end = f.offset + f.size;
        if (end > total) total = end;
    }
    return total;
}

void StructDefinition::SortFields() {
    std::sort(fields.begin(), fields.end(),
              [](const StructField& a, const StructField& b) {
                  return a.offset < b.offset;
              });
}

void StructDefinition::AddField(const std::string& fieldName, FieldType type, size_t offset) {
    StructField field;
    field.name = fieldName;
    field.type = type;
    field.offset = offset;
    field.size = GetFieldSize(type);
    fields.push_back(field);
    SortFields();
}

void StructDefinition::RemoveField(size_t index) {
    if (index < fields.size()) {
        fields.erase(fields.begin() + static_cast<ptrdiff_t>(index));
    }
}

std::string StructDefinition::GenerateCppStruct() const {
    std::ostringstream ss;
    ss << "struct " << (name.empty() ? "UnknownStruct" : name) << " {\n";

    size_t prevEnd = 0;
    for (auto& f : fields) {
        // Insert padding if gap
        if (f.offset > prevEnd) {
            size_t gap = f.offset - prevEnd;
            ss << "    uint8_t _padding_0x" << std::hex << prevEnd
               << "[" << std::dec << gap << "];\n";
        }

        // Comment with offset
        ss << "    ";
        switch (f.type) {
            case FieldType::Int8:    ss << "int8_t"; break;
            case FieldType::Int16:   ss << "int16_t"; break;
            case FieldType::Int32:   ss << "int32_t"; break;
            case FieldType::Int64:   ss << "int64_t"; break;
            case FieldType::UInt8:   ss << "uint8_t"; break;
            case FieldType::UInt16:  ss << "uint16_t"; break;
            case FieldType::UInt32:  ss << "uint32_t"; break;
            case FieldType::UInt64:  ss << "uint64_t"; break;
            case FieldType::Float:   ss << "float"; break;
            case FieldType::Double:  ss << "double"; break;
            case FieldType::Pointer: ss << "void*"; break;
            case FieldType::String:  ss << "char"; break;
            case FieldType::Padding: ss << "uint8_t"; break;
        }

        ss << " " << (f.name.empty() ? "field_" + std::to_string(f.offset) : f.name);
        if (f.type == FieldType::String) {
            ss << "[" << f.size << "]";
        }
        ss << "; // 0x" << std::hex << f.offset;
        if (!f.comment.empty()) ss << " - " << f.comment;
        ss << "\n";

        prevEnd = f.offset + f.size;
    }

    ss << "}; // size: 0x" << std::hex << GetTotalSize() << "\n";
    return ss.str();
}

FieldType StructDefinition::GuessType(const uint8_t* data, size_t maxLen) {
    if (maxLen < 4) return FieldType::UInt8;

    // Check if it looks like a pointer (in typical user-space range)
    if (maxLen >= 8) {
        uint64_t val64;
        std::memcpy(&val64, data, 8);
        if (val64 > 0x10000 && val64 < 0x00007FFFFFFFFFFF) {
            return FieldType::Pointer;
        }
    }

    // Check if it looks like a float
    float fval;
    std::memcpy(&fval, data, 4);
    if (std::isfinite(fval) && fval != 0.0f && std::fabs(fval) > 1e-10f && std::fabs(fval) < 1e10f) {
        return FieldType::Float;
    }

    // Check if it looks like a small integer
    int32_t ival;
    std::memcpy(&ival, data, 4);
    if (ival >= -1000000 && ival <= 1000000) {
        return FieldType::Int32;
    }

    // Check if ASCII string
    bool isString = true;
    for (size_t i = 0; i < (std::min)(maxLen, (size_t)8); i++) {
        if (data[i] == 0) break;
        if (data[i] < 32 || data[i] > 126) { isString = false; break; }
    }
    if (isString && data[0] >= 32) {
        return FieldType::String;
    }

    return FieldType::Int32;
}

// ── StructureDissector ───────────────────────────────────

bool StructureDissector::ReadFieldValue(uintptr_t baseAddress, size_t offset,
                                         size_t size, void* outBuffer) const {
    if (!m_hProcess) return false;
    SIZE_T bytesRead = 0;
    return ReadProcessMemory(m_hProcess, reinterpret_cast<LPCVOID>(baseAddress + offset),
                             outBuffer, size, &bytesRead) && bytesRead == size;
}

bool StructureDissector::ReadBytes(uintptr_t address, size_t size,
                                    std::vector<uint8_t>& outBytes) const {
    if (!m_hProcess) return false;
    outBytes.resize(size);
    SIZE_T bytesRead = 0;
    if (ReadProcessMemory(m_hProcess, reinterpret_cast<LPCVOID>(address),
                          outBytes.data(), size, &bytesRead)) {
        outBytes.resize(bytesRead);
        return true;
    }
    return false;
}

std::string StructureDissector::FormatFieldValue(uintptr_t baseAddress,
                                                  const StructField& field) const {
    if (!m_hProcess) return "N/A";

    uint8_t buf[256] = {};
    size_t readSize = (std::min)(field.size, (size_t)256);
    if (!ReadFieldValue(baseAddress, field.offset, readSize, buf)) {
        return "<read error>";
    }

    std::ostringstream ss;
    switch (field.type) {
        case FieldType::Int8:    ss << static_cast<int>(*reinterpret_cast<int8_t*>(buf)); break;
        case FieldType::Int16:   ss << *reinterpret_cast<int16_t*>(buf); break;
        case FieldType::Int32:   ss << *reinterpret_cast<int32_t*>(buf); break;
        case FieldType::Int64:   ss << *reinterpret_cast<int64_t*>(buf); break;
        case FieldType::UInt8:   ss << static_cast<unsigned>(*reinterpret_cast<uint8_t*>(buf)); break;
        case FieldType::UInt16:  ss << *reinterpret_cast<uint16_t*>(buf); break;
        case FieldType::UInt32:  ss << *reinterpret_cast<uint32_t*>(buf); break;
        case FieldType::UInt64:  ss << *reinterpret_cast<uint64_t*>(buf); break;
        case FieldType::Float: {
            float f;
            std::memcpy(&f, buf, 4);
            ss << std::fixed << std::setprecision(4) << f;
            break;
        }
        case FieldType::Double: {
            double d;
            std::memcpy(&d, buf, 8);
            ss << std::fixed << std::setprecision(6) << d;
            break;
        }
        case FieldType::Pointer: {
            uint64_t ptr;
            std::memcpy(&ptr, buf, 8);
            ss << "0x" << std::hex << std::uppercase << ptr;
            break;
        }
        case FieldType::String: {
            std::string s(reinterpret_cast<char*>(buf), readSize);
            // Null-terminate
            auto pos = s.find('\0');
            if (pos != std::string::npos) s.resize(pos);
            ss << "\"" << s << "\"";
            break;
        }
        case FieldType::Padding:
            ss << "...";
            break;
    }
    return ss.str();
}

std::string StructureDissector::FormatFieldHex(uintptr_t baseAddress,
                                                const StructField& field) const {
    if (!m_hProcess) return "";

    uint8_t buf[256] = {};
    size_t readSize = (std::min)(field.size, (size_t)256);
    if (!ReadFieldValue(baseAddress, field.offset, readSize, buf)) {
        return "<error>";
    }

    std::ostringstream ss;
    for (size_t i = 0; i < readSize; i++) {
        if (i > 0) ss << " ";
        ss << std::hex << std::uppercase << std::setw(2) << std::setfill('0')
           << static_cast<unsigned>(buf[i]);
    }
    return ss.str();
}

StructDefinition StructureDissector::AutoDetect(uintptr_t baseAddress, size_t totalSize) const {
    StructDefinition def;
    def.name = "AutoStruct";
    def.baseAddress = baseAddress;

    std::vector<uint8_t> data;
    if (!ReadBytes(baseAddress, totalSize, data)) return def;
    if (data.empty()) return def;

    size_t offset = 0;
    int fieldNum = 0;
    while (offset < data.size()) {
        size_t remaining = data.size() - offset;
        FieldType guessed = StructDefinition::GuessType(data.data() + offset, remaining);
        size_t fsize = StructDefinition::GetFieldSize(guessed);
        if (offset + fsize > data.size()) break;

        StructField field;
        field.name = "field_" + std::to_string(fieldNum++);
        field.type = guessed;
        field.offset = offset;
        field.size = fsize;
        def.fields.push_back(field);

        offset += fsize;
    }

    return def;
}

} // namespace memforge
