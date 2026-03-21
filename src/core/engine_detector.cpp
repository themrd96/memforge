#include "core/engine_detector.h"
#include "core/process_manager.h"
#include <algorithm>

namespace memforge {

const char* EngineDetector::GetEngineName(GameEngine engine) {
    switch (engine) {
        case GameEngine::Unknown:       return "Unknown";
        case GameEngine::Unity_Mono:    return "Unity (Mono)";
        case GameEngine::Unity_IL2CPP:  return "Unity (IL2CPP)";
        case GameEngine::UnrealEngine:  return "Unreal Engine";
        case GameEngine::Source:        return "Source Engine";
        case GameEngine::Source2:       return "Source 2";
        case GameEngine::Godot:         return "Godot Engine";
        case GameEngine::GameMaker:     return "GameMaker";
        case GameEngine::RPGMaker:      return "RPG Maker";
    }
    return "Unknown";
}

static std::string ToLower(const std::string& s) {
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

bool EngineDetector::HasModule(DWORD pid, const std::string& moduleName) {
    auto modules = ProcessManager::GetModules(pid);
    std::string target = ToLower(moduleName);
    for (auto& mod : modules) {
        if (ToLower(mod.name) == target) return true;
    }
    return false;
}

std::vector<std::string> EngineDetector::GetModuleNames(DWORD pid) {
    auto modules = ProcessManager::GetModules(pid);
    std::vector<std::string> names;
    names.reserve(modules.size());
    for (auto& mod : modules) {
        names.push_back(mod.name);
    }
    return names;
}

EngineInfo EngineDetector::Detect(HANDLE hProcess, DWORD pid) {
    EngineInfo info;
    info.engine = GameEngine::Unknown;
    info.engineName = "Unknown";

    // Check each engine in priority order
    if (CheckUnityMono(pid, info)) return info;
    if (CheckUnityIL2CPP(pid, info)) return info;
    if (CheckUnreal(pid, info)) return info;
    if (CheckSource(pid, info)) return info;
    if (CheckGodot(pid, info)) return info;
    if (CheckGameMaker(pid, info)) return info;
    if (CheckRPGMaker(pid, info)) return info;

    return info;
}

bool EngineDetector::CheckUnityMono(DWORD pid, EngineInfo& info) {
    bool hasMono = HasModule(pid, "mono.dll") ||
                   HasModule(pid, "mono-2.0-bdwgc.dll");
    if (hasMono) {
        info.engine = GameEngine::Unity_Mono;
        info.engineName = "Unity (Mono)";
        info.notes = "Mono scripting backend. Use mono API for class/method enumeration. "
                     "Assemblies in Managed/ folder.";
        info.relevantModules.push_back("mono.dll");
        info.relevantModules.push_back("mono-2.0-bdwgc.dll");
        info.relevantModules.push_back("UnityPlayer.dll");
        return true;
    }
    return false;
}

bool EngineDetector::CheckUnityIL2CPP(DWORD pid, EngineInfo& info) {
    bool hasIL2CPP = HasModule(pid, "GameAssembly.dll") ||
                     HasModule(pid, "il2cpp.dll");
    if (hasIL2CPP) {
        info.engine = GameEngine::Unity_IL2CPP;
        info.engineName = "Unity (IL2CPP)";
        info.notes = "IL2CPP scripting backend. C# compiled to C++. "
                     "Use il2cpp dumper for class info. GameAssembly.dll contains game code.";
        info.relevantModules.push_back("GameAssembly.dll");
        info.relevantModules.push_back("UnityPlayer.dll");
        return true;
    }
    return false;
}

bool EngineDetector::CheckUnreal(DWORD pid, EngineInfo& info) {
    auto modules = GetModuleNames(pid);
    for (auto& name : modules) {
        std::string lower = ToLower(name);
        if (lower.find("ue4") != std::string::npos ||
            lower.find("ue5") != std::string::npos ||
            lower.find("unrealengine") != std::string::npos ||
            lower == "engine.dll") {
            // Differentiate from Source engine by checking for other Unreal modules
            if (HasModule(pid, "PhysX3_x64.dll") ||
                HasModule(pid, "nvToolsExt64_1.dll") ||
                lower.find("ue4") != std::string::npos ||
                lower.find("ue5") != std::string::npos) {
                info.engine = GameEngine::UnrealEngine;
                info.engineName = "Unreal Engine";
                info.notes = "Unreal Engine game. UObject system uses GObjects/GNames arrays. "
                             "Look for GWorld pointer for world state.";
                info.relevantModules.push_back(name);
                return true;
            }
        }
    }
    return false;
}

bool EngineDetector::CheckSource(DWORD pid, EngineInfo& info) {
    bool hasEngine = HasModule(pid, "engine.dll");
    bool hasClient = HasModule(pid, "client.dll");
    bool hasServer = HasModule(pid, "server.dll");

    // Source 2
    if (HasModule(pid, "engine2.dll") || (HasModule(pid, "client.dll") && HasModule(pid, "schemasystem.dll"))) {
        info.engine = GameEngine::Source2;
        info.engineName = "Source 2";
        info.notes = "Source 2 engine. Uses schema system for class info. "
                     "client.dll and engine2.dll are key modules.";
        info.relevantModules.push_back("engine2.dll");
        info.relevantModules.push_back("client.dll");
        info.relevantModules.push_back("schemasystem.dll");
        return true;
    }

    // Source 1
    if (hasEngine && hasClient) {
        info.engine = GameEngine::Source;
        info.engineName = "Source Engine";
        info.notes = "Valve Source engine. Use NetVar offsets from client.dll. "
                     "Entity list accessible via client.dll.";
        info.relevantModules.push_back("engine.dll");
        info.relevantModules.push_back("client.dll");
        if (hasServer) info.relevantModules.push_back("server.dll");
        return true;
    }

    return false;
}

bool EngineDetector::CheckGodot(DWORD pid, EngineInfo& info) {
    auto modules = GetModuleNames(pid);
    for (auto& name : modules) {
        std::string lower = ToLower(name);
        if (lower.find("godot") != std::string::npos) {
            info.engine = GameEngine::Godot;
            info.engineName = "Godot Engine";
            info.notes = "Godot engine game. GDScript or C# backend. "
                         "Check for node tree in memory.";
            info.relevantModules.push_back(name);
            return true;
        }
    }
    return false;
}

bool EngineDetector::CheckGameMaker(DWORD pid, EngineInfo& info) {
    auto modules = GetModuleNames(pid);
    for (auto& name : modules) {
        std::string lower = ToLower(name);
        if (lower.find("gamemaker") != std::string::npos ||
            lower == "data.win" || lower == "yoyo_runner.dll") {
            info.engine = GameEngine::GameMaker;
            info.engineName = "GameMaker";
            info.notes = "GameMaker game. Variables stored in instance structures. "
                         "Look for data.win in game directory.";
            info.relevantModules.push_back(name);
            return true;
        }
    }
    return false;
}

bool EngineDetector::CheckRPGMaker(DWORD pid, EngineInfo& info) {
    auto modules = GetModuleNames(pid);
    for (auto& name : modules) {
        std::string lower = ToLower(name);
        if (lower.find("rgss") != std::string::npos ||
            lower.find("rpgmaker") != std::string::npos) {
            info.engine = GameEngine::RPGMaker;
            info.engineName = "RPG Maker";
            info.notes = "RPG Maker game. Ruby/RGSS scripting. "
                         "Game data in .rxdata/.rvdata/.rvdata2 files.";
            info.relevantModules.push_back(name);
            return true;
        }
    }
    return false;
}

} // namespace memforge
