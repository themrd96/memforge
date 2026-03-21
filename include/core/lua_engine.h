#pragma once
#include <Windows.h>
#include <string>
#include <vector>
#include <functional>

// Forward declare lua_State in global scope (Lua is C code)
struct lua_State;

namespace memforge {

class LuaEngine {
public:
    LuaEngine();
    ~LuaEngine();

    void Initialize(HANDLE hProcess, DWORD pid);
    void Shutdown();

    struct ScriptResult {
        bool success;
        std::string output;
        std::string error;
    };

    ScriptResult Execute(const std::string& script);
    ScriptResult ExecuteFile(const std::string& path);

    bool IsInitialized() const { return m_luaState != nullptr; }

    // Output callback for print statements
    using OutputCallback = std::function<void(const std::string&)>;
    void SetOutputCallback(OutputCallback cb) { m_outputCb = cb; }

    // Access process handle/pid (used by API bindings)
    HANDLE GetProcessHandle() const { return m_hProcess; }
    DWORD GetProcessId() const { return m_pid; }

private:
    void RegisterAPI();

    lua_State* m_luaState = nullptr;
    HANDLE m_hProcess = nullptr;
    DWORD m_pid = 0;
    OutputCallback m_outputCb;
    std::string m_outputBuffer;
};

} // namespace memforge
