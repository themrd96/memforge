#pragma once
#include <string>
#include <vector>
#include <optional>
#include "core/value_freezer.h"
#include "core/structure_dissector.h"
#include "core/pointer_scanner.h"

namespace memforge {

struct CheatTable {
    std::string gameName;
    std::string gameExe;
    std::string description;
    std::string author;
    std::string version;

    // Saved frozen values
    std::vector<FrozenValue> frozenValues;

    // Saved structure definitions
    std::vector<StructDefinition> structures;

    // Saved pointer paths
    std::vector<PointerPath> pointerPaths;

    // Saved scripts
    struct SavedScript {
        std::string name;
        std::string code;
    };
    std::vector<SavedScript> scripts;

    // Save to file
    bool SaveToFile(const std::string& path) const;

    // Load from file
    static std::optional<CheatTable> LoadFromFile(const std::string& path);
};

// Minimal JSON-like serializer (no external deps)
class MftSerializer {
public:
    // Write helpers
    static std::string Serialize(const CheatTable& table);

    // Read helpers
    static std::optional<CheatTable> Deserialize(const std::string& data);

    // Escape/unescape strings for the format
    static std::string EscapeString(const std::string& s);
    static std::string UnescapeString(const std::string& s);
};

} // namespace memforge
