#include "core/cheat_table.h"
#include <fstream>
#include <sstream>
#include <algorithm>

namespace memforge {

// ── MftSerializer ────────────────────────────────────────

std::string MftSerializer::EscapeString(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"':  out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:   out += c; break;
        }
    }
    return out;
}

std::string MftSerializer::UnescapeString(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            switch (s[i + 1]) {
                case '\\': out += '\\'; i++; break;
                case '"':  out += '"'; i++; break;
                case 'n':  out += '\n'; i++; break;
                case 'r':  out += '\r'; i++; break;
                case 't':  out += '\t'; i++; break;
                default:   out += s[i]; break;
            }
        } else {
            out += s[i];
        }
    }
    return out;
}

// Simple key=value line-based format instead of true JSON
// This avoids needing a full JSON parser

static void WriteString(std::ostringstream& ss, const std::string& key,
                        const std::string& value, int indent = 0) {
    for (int i = 0; i < indent; i++) ss << "  ";
    ss << key << " = \"" << MftSerializer::EscapeString(value) << "\"\n";
}

static void WriteInt(std::ostringstream& ss, const std::string& key,
                     int64_t value, int indent = 0) {
    for (int i = 0; i < indent; i++) ss << "  ";
    ss << key << " = " << value << "\n";
}

static void WriteUInt(std::ostringstream& ss, const std::string& key,
                      uint64_t value, int indent = 0) {
    for (int i = 0; i < indent; i++) ss << "  ";
    ss << key << " = " << value << "\n";
}

static void WriteFloat(std::ostringstream& ss, const std::string& key,
                       double value, int indent = 0) {
    for (int i = 0; i < indent; i++) ss << "  ";
    ss << key << " = " << std::fixed << value << "\n";
}

static void WriteBool(std::ostringstream& ss, const std::string& key,
                      bool value, int indent = 0) {
    for (int i = 0; i < indent; i++) ss << "  ";
    ss << key << " = " << (value ? "true" : "false") << "\n";
}

std::string MftSerializer::Serialize(const CheatTable& table) {
    std::ostringstream ss;

    ss << "[MemForgeTable]\n";
    WriteString(ss, "gameName", table.gameName);
    WriteString(ss, "gameExe", table.gameExe);
    WriteString(ss, "description", table.description);
    WriteString(ss, "author", table.author);
    WriteString(ss, "version", table.version);

    // Frozen values
    if (!table.frozenValues.empty()) {
        ss << "\n[FrozenValues]\n";
        WriteInt(ss, "count", static_cast<int64_t>(table.frozenValues.size()));
        for (size_t i = 0; i < table.frozenValues.size(); i++) {
            auto& fv = table.frozenValues[i];
            ss << "\n  [FrozenValue." << i << "]\n";
            WriteUInt(ss, "address", fv.address, 1);
            WriteInt(ss, "type", static_cast<int64_t>(fv.type), 1);
            WriteString(ss, "description", fv.description, 1);
            WriteBool(ss, "active", fv.active, 1);

            // Serialize the value based on type
            std::string valStr = MemoryScanner::ValueToString(fv.value, fv.type);
            WriteString(ss, "value", valStr, 1);
        }
    }

    // Structure definitions
    if (!table.structures.empty()) {
        ss << "\n[Structures]\n";
        WriteInt(ss, "count", static_cast<int64_t>(table.structures.size()));
        for (size_t i = 0; i < table.structures.size(); i++) {
            auto& st = table.structures[i];
            ss << "\n  [Structure." << i << "]\n";
            WriteString(ss, "name", st.name, 1);
            WriteUInt(ss, "baseAddress", st.baseAddress, 1);
            WriteInt(ss, "fieldCount", static_cast<int64_t>(st.fields.size()), 1);
            for (size_t j = 0; j < st.fields.size(); j++) {
                auto& f = st.fields[j];
                ss << "\n    [Field." << i << "." << j << "]\n";
                WriteString(ss, "name", f.name, 2);
                WriteInt(ss, "type", static_cast<int64_t>(f.type), 2);
                WriteUInt(ss, "offset", f.offset, 2);
                WriteUInt(ss, "size", f.size, 2);
                WriteString(ss, "comment", f.comment, 2);
            }
        }
    }

    // Pointer paths
    if (!table.pointerPaths.empty()) {
        ss << "\n[PointerPaths]\n";
        WriteInt(ss, "count", static_cast<int64_t>(table.pointerPaths.size()));
        for (size_t i = 0; i < table.pointerPaths.size(); i++) {
            auto& pp = table.pointerPaths[i];
            ss << "\n  [PointerPath." << i << "]\n";
            WriteUInt(ss, "baseAddress", pp.baseAddress, 1);
            WriteString(ss, "moduleName", pp.moduleName, 1);
            WriteInt(ss, "offsetCount", static_cast<int64_t>(pp.offsets.size()), 1);
            for (size_t j = 0; j < pp.offsets.size(); j++) {
                WriteInt(ss, "offset." + std::to_string(j), pp.offsets[j], 1);
            }
        }
    }

    // Scripts
    if (!table.scripts.empty()) {
        ss << "\n[Scripts]\n";
        WriteInt(ss, "count", static_cast<int64_t>(table.scripts.size()));
        for (size_t i = 0; i < table.scripts.size(); i++) {
            auto& sc = table.scripts[i];
            ss << "\n  [Script." << i << "]\n";
            WriteString(ss, "name", sc.name, 1);
            WriteString(ss, "code", sc.code, 1);
        }
    }

    return ss.str();
}

// ── Simple parser ────────────────────────────────────────

struct ParsedLine {
    std::string key;
    std::string value;
    bool isSection = false;
    std::string sectionName;
};

static ParsedLine ParseLine(const std::string& line) {
    ParsedLine pl;

    std::string trimmed = line;
    // Trim leading whitespace
    size_t start = trimmed.find_first_not_of(" \t");
    if (start == std::string::npos) return pl;
    trimmed = trimmed.substr(start);

    // Section header
    if (trimmed.front() == '[' && trimmed.back() == ']') {
        pl.isSection = true;
        pl.sectionName = trimmed.substr(1, trimmed.size() - 2);
        return pl;
    }

    // Key = value
    auto eqPos = trimmed.find(" = ");
    if (eqPos == std::string::npos) return pl;

    pl.key = trimmed.substr(0, eqPos);
    std::string val = trimmed.substr(eqPos + 3);

    // Check if quoted string
    if (val.size() >= 2 && val.front() == '"' && val.back() == '"') {
        pl.value = MftSerializer::UnescapeString(val.substr(1, val.size() - 2));
    } else {
        pl.value = val;
    }

    return pl;
}

std::optional<CheatTable> MftSerializer::Deserialize(const std::string& data) {
    CheatTable table;

    std::istringstream iss(data);
    std::string line;
    std::string currentSection;

    // Temporary storage for current item being parsed
    FrozenValue currentFV = {};
    StructDefinition currentStruct;
    StructField currentField;
    PointerPath currentPP;
    CheatTable::SavedScript currentScript;

    int frozenIdx = -1;
    int structIdx = -1;
    int fieldIdx = -1;
    int ppIdx = -1;
    int scriptIdx = -1;

    while (std::getline(iss, line)) {
        if (line.empty()) continue;

        ParsedLine pl = ParseLine(line);

        if (pl.isSection) {
            currentSection = pl.sectionName;

            // Check for indexed sections
            if (currentSection.find("FrozenValue.") == 0) {
                if (frozenIdx >= 0) table.frozenValues.push_back(currentFV);
                currentFV = {};
                frozenIdx++;
            } else if (currentSection.find("Field.") == 0) {
                if (fieldIdx >= 0 && !currentStruct.fields.empty()) {
                    // field already added via AddField or push_back below
                }
                currentField = {};
                fieldIdx++;
            } else if (currentSection.find("Structure.") == 0) {
                if (structIdx >= 0) table.structures.push_back(currentStruct);
                currentStruct = {};
                structIdx++;
                fieldIdx = -1;
            } else if (currentSection.find("PointerPath.") == 0) {
                if (ppIdx >= 0) table.pointerPaths.push_back(currentPP);
                currentPP = {};
                ppIdx++;
            } else if (currentSection.find("Script.") == 0) {
                if (scriptIdx >= 0) table.scripts.push_back(currentScript);
                currentScript = {};
                scriptIdx++;
            }
            continue;
        }

        if (pl.key.empty()) continue;

        // Route to the right section
        if (currentSection == "MemForgeTable") {
            if (pl.key == "gameName") table.gameName = pl.value;
            else if (pl.key == "gameExe") table.gameExe = pl.value;
            else if (pl.key == "description") table.description = pl.value;
            else if (pl.key == "author") table.author = pl.value;
            else if (pl.key == "version") table.version = pl.value;
        }
        else if (currentSection.find("FrozenValue.") == 0) {
            if (pl.key == "address") currentFV.address = std::stoull(pl.value);
            else if (pl.key == "type") currentFV.type = static_cast<ValueType>(std::stoi(pl.value));
            else if (pl.key == "description") currentFV.description = pl.value;
            else if (pl.key == "active") currentFV.active = (pl.value == "true");
            else if (pl.key == "value") {
                currentFV.value = MemoryScanner::StringToValue(pl.value, currentFV.type);
            }
        }
        else if (currentSection.find("Field.") == 0) {
            if (pl.key == "name") currentField.name = pl.value;
            else if (pl.key == "type") currentField.type = static_cast<FieldType>(std::stoi(pl.value));
            else if (pl.key == "offset") currentField.offset = std::stoull(pl.value);
            else if (pl.key == "size") {
                currentField.size = std::stoull(pl.value);
                // Add completed field to current struct
                currentStruct.fields.push_back(currentField);
            }
            else if (pl.key == "comment") {
                if (!currentStruct.fields.empty()) {
                    currentStruct.fields.back().comment = pl.value;
                }
            }
        }
        else if (currentSection.find("Structure.") == 0 &&
                 currentSection.find("Field.") == std::string::npos) {
            if (pl.key == "name") currentStruct.name = pl.value;
            else if (pl.key == "baseAddress") currentStruct.baseAddress = std::stoull(pl.value);
        }
        else if (currentSection.find("PointerPath.") == 0) {
            if (pl.key == "baseAddress") currentPP.baseAddress = std::stoull(pl.value);
            else if (pl.key == "moduleName") currentPP.moduleName = pl.value;
            else if (pl.key.find("offset.") == 0) {
                currentPP.offsets.push_back(std::stoll(pl.value));
            }
        }
        else if (currentSection.find("Script.") == 0) {
            if (pl.key == "name") currentScript.name = pl.value;
            else if (pl.key == "code") currentScript.code = pl.value;
        }
    }

    // Flush last items
    if (frozenIdx >= 0) table.frozenValues.push_back(currentFV);
    if (structIdx >= 0) table.structures.push_back(currentStruct);
    if (ppIdx >= 0) table.pointerPaths.push_back(currentPP);
    if (scriptIdx >= 0) table.scripts.push_back(currentScript);

    return table;
}

// ── CheatTable ───────────────────────────────────────────

bool CheatTable::SaveToFile(const std::string& path) const {
    std::string data = MftSerializer::Serialize(*this);
    std::ofstream ofs(path, std::ios::binary);
    if (!ofs.is_open()) return false;
    ofs.write(data.c_str(), static_cast<std::streamsize>(data.size()));
    return ofs.good();
}

std::optional<CheatTable> CheatTable::LoadFromFile(const std::string& path) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs.is_open()) return std::nullopt;
    std::string data((std::istreambuf_iterator<char>(ifs)),
                     std::istreambuf_iterator<char>());
    return MftSerializer::Deserialize(data);
}

} // namespace memforge
