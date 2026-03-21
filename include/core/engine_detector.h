#pragma once
#include <Windows.h>
#include <string>
#include <vector>

namespace memforge {

enum class GameEngine {
    Unknown,
    Unity_Mono,
    Unity_IL2CPP,
    UnrealEngine,
    Source,
    Source2,
    Godot,
    GameMaker,
    RPGMaker
};

struct EngineInfo {
    GameEngine engine = GameEngine::Unknown;
    std::string engineName;
    std::string engineVersion;   // if detectable
    std::vector<std::string> relevantModules;
    std::string notes;           // engine-specific tips
};

class EngineDetector {
public:
    static EngineInfo Detect(HANDLE hProcess, DWORD pid);

    static const char* GetEngineName(GameEngine engine);

private:
    static bool CheckUnityMono(DWORD pid, EngineInfo& info);
    static bool CheckUnityIL2CPP(DWORD pid, EngineInfo& info);
    static bool CheckUnreal(DWORD pid, EngineInfo& info);
    static bool CheckSource(DWORD pid, EngineInfo& info);
    static bool CheckGodot(DWORD pid, EngineInfo& info);
    static bool CheckGameMaker(DWORD pid, EngineInfo& info);
    static bool CheckRPGMaker(DWORD pid, EngineInfo& info);

    // Helper: check if a module is loaded in a process
    static bool HasModule(DWORD pid, const std::string& moduleName);
    static std::vector<std::string> GetModuleNames(DWORD pid);
};

} // namespace memforge
