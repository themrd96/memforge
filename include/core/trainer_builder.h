#pragma once
#include <Windows.h>
#include <string>
#include <vector>

namespace memforge {

struct TrainerCheat {
    std::string name;
    std::string description;
    int hotkeyVk = 0;
    bool hotkeyCtrl = false;
    bool hotkeyAlt = false;

    enum class CheatType { FreezeValue, SetValue, NopBytes, RunScript };
    CheatType type = CheatType::FreezeValue;

    // For FreezeValue / SetValue
    std::string aobPattern;
    int aobOffset = 0;
    std::string valueType;  // "int32", "float", etc.
    std::string value;

    // For NopBytes
    int nopCount = 0;

    // For RunScript
    std::string luaScript;
};

struct TrainerConfig {
    std::string gameName;
    std::string gameExe;
    std::string trainerName;
    std::string author;
    std::vector<TrainerCheat> cheats;
};

class TrainerBuilder {
public:
    static bool GenerateTrainerSource(const TrainerConfig& config,
                                      const std::string& outputPath);

    static bool GenerateBuildScript(const std::string& sourcePath,
                                    const std::string& outputExePath);

    static bool BuildTrainer(const TrainerConfig& config,
                             const std::string& outputExePath);
};

} // namespace memforge
