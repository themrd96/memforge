#include "core/lua_engine.h"
#include "core/memory_scanner.h"
#include "core/memory_writer.h"
#include "core/process_manager.h"

// Lua is C code, must wrap includes
extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}

#include <sstream>
#include <thread>
#include <chrono>

namespace memforge {

// Store engine pointer in Lua registry for API callbacks
static const char* LUAENGINE_REGISTRY_KEY = "MemForgeLuaEngine";

static LuaEngine* GetEngine(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, LUAENGINE_REGISTRY_KEY);
    auto* engine = static_cast<LuaEngine*>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    return engine;
}

// ── Lua API functions ────────────────────────────────────

static int lua_memforge_readInt(lua_State* L) {
    auto* engine = GetEngine(L);
    if (!engine || !engine->GetProcessHandle()) {
        lua_pushnil(L);
        return 1;
    }
    uintptr_t addr = static_cast<uintptr_t>(luaL_checkinteger(L, 1));
    int32_t val = 0;
    SIZE_T bytesRead = 0;
    if (ReadProcessMemory(engine->GetProcessHandle(), reinterpret_cast<LPCVOID>(addr),
                          &val, sizeof(val), &bytesRead)) {
        lua_pushinteger(L, val);
    } else {
        lua_pushnil(L);
    }
    return 1;
}

static int lua_memforge_readFloat(lua_State* L) {
    auto* engine = GetEngine(L);
    if (!engine || !engine->GetProcessHandle()) {
        lua_pushnil(L);
        return 1;
    }
    uintptr_t addr = static_cast<uintptr_t>(luaL_checkinteger(L, 1));
    float val = 0.0f;
    SIZE_T bytesRead = 0;
    if (ReadProcessMemory(engine->GetProcessHandle(), reinterpret_cast<LPCVOID>(addr),
                          &val, sizeof(val), &bytesRead)) {
        lua_pushnumber(L, static_cast<double>(val));
    } else {
        lua_pushnil(L);
    }
    return 1;
}

static int lua_memforge_readDouble(lua_State* L) {
    auto* engine = GetEngine(L);
    if (!engine || !engine->GetProcessHandle()) {
        lua_pushnil(L);
        return 1;
    }
    uintptr_t addr = static_cast<uintptr_t>(luaL_checkinteger(L, 1));
    double val = 0.0;
    SIZE_T bytesRead = 0;
    if (ReadProcessMemory(engine->GetProcessHandle(), reinterpret_cast<LPCVOID>(addr),
                          &val, sizeof(val), &bytesRead)) {
        lua_pushnumber(L, val);
    } else {
        lua_pushnil(L);
    }
    return 1;
}

static int lua_memforge_readString(lua_State* L) {
    auto* engine = GetEngine(L);
    if (!engine || !engine->GetProcessHandle()) {
        lua_pushnil(L);
        return 1;
    }
    uintptr_t addr = static_cast<uintptr_t>(luaL_checkinteger(L, 1));
    int maxLen = static_cast<int>(luaL_optinteger(L, 2, 256));
    if (maxLen <= 0 || maxLen > 4096) maxLen = 256;

    std::vector<char> buf(maxLen + 1, 0);
    SIZE_T bytesRead = 0;
    if (ReadProcessMemory(engine->GetProcessHandle(), reinterpret_cast<LPCVOID>(addr),
                          buf.data(), maxLen, &bytesRead)) {
        buf[bytesRead] = 0;
        lua_pushstring(L, buf.data());
    } else {
        lua_pushnil(L);
    }
    return 1;
}

static int lua_memforge_readBytes(lua_State* L) {
    auto* engine = GetEngine(L);
    if (!engine || !engine->GetProcessHandle()) {
        lua_pushnil(L);
        return 1;
    }
    uintptr_t addr = static_cast<uintptr_t>(luaL_checkinteger(L, 1));
    int count = static_cast<int>(luaL_checkinteger(L, 2));
    if (count <= 0 || count > 65536) {
        lua_pushnil(L);
        return 1;
    }

    std::vector<uint8_t> buf(count);
    SIZE_T bytesRead = 0;
    if (ReadProcessMemory(engine->GetProcessHandle(), reinterpret_cast<LPCVOID>(addr),
                          buf.data(), count, &bytesRead)) {
        lua_newtable(L);
        for (SIZE_T i = 0; i < bytesRead; i++) {
            lua_pushinteger(L, buf[i]);
            lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
        }
    } else {
        lua_pushnil(L);
    }
    return 1;
}

static int lua_memforge_writeInt(lua_State* L) {
    auto* engine = GetEngine(L);
    if (!engine || !engine->GetProcessHandle()) {
        lua_pushboolean(L, 0);
        return 1;
    }
    uintptr_t addr = static_cast<uintptr_t>(luaL_checkinteger(L, 1));
    int32_t val = static_cast<int32_t>(luaL_checkinteger(L, 2));
    SIZE_T bytesWritten = 0;
    BOOL ok = WriteProcessMemory(engine->GetProcessHandle(), reinterpret_cast<LPVOID>(addr),
                                 &val, sizeof(val), &bytesWritten);
    lua_pushboolean(L, ok);
    return 1;
}

static int lua_memforge_writeFloat(lua_State* L) {
    auto* engine = GetEngine(L);
    if (!engine || !engine->GetProcessHandle()) {
        lua_pushboolean(L, 0);
        return 1;
    }
    uintptr_t addr = static_cast<uintptr_t>(luaL_checkinteger(L, 1));
    float val = static_cast<float>(luaL_checknumber(L, 2));
    SIZE_T bytesWritten = 0;
    BOOL ok = WriteProcessMemory(engine->GetProcessHandle(), reinterpret_cast<LPVOID>(addr),
                                 &val, sizeof(val), &bytesWritten);
    lua_pushboolean(L, ok);
    return 1;
}

static int lua_memforge_writeBytes(lua_State* L) {
    auto* engine = GetEngine(L);
    if (!engine || !engine->GetProcessHandle()) {
        lua_pushboolean(L, 0);
        return 1;
    }
    uintptr_t addr = static_cast<uintptr_t>(luaL_checkinteger(L, 1));
    luaL_checktype(L, 2, LUA_TTABLE);

    size_t len = lua_rawlen(L, 2);
    std::vector<uint8_t> bytes(len);
    for (size_t i = 0; i < len; i++) {
        lua_rawgeti(L, 2, static_cast<lua_Integer>(i + 1));
        bytes[i] = static_cast<uint8_t>(lua_tointeger(L, -1));
        lua_pop(L, 1);
    }

    SIZE_T bytesWritten = 0;
    BOOL ok = WriteProcessMemory(engine->GetProcessHandle(), reinterpret_cast<LPVOID>(addr),
                                 bytes.data(), bytes.size(), &bytesWritten);
    lua_pushboolean(L, ok);
    return 1;
}

static int lua_memforge_getProcessId(lua_State* L) {
    auto* engine = GetEngine(L);
    if (!engine) {
        lua_pushinteger(L, 0);
        return 1;
    }
    lua_pushinteger(L, engine->GetProcessId());
    return 1;
}

static int lua_memforge_getModuleBase(lua_State* L) {
    auto* engine = GetEngine(L);
    if (!engine) {
        lua_pushinteger(L, 0);
        return 1;
    }
    const char* modName = luaL_checkstring(L, 1);
    auto modules = ProcessManager::GetModules(engine->GetProcessId());
    for (auto& mod : modules) {
        if (mod.name == modName) {
            lua_pushinteger(L, static_cast<lua_Integer>(mod.baseAddress));
            return 1;
        }
    }
    lua_pushinteger(L, 0);
    return 1;
}

static int lua_memforge_getModules(lua_State* L) {
    auto* engine = GetEngine(L);
    if (!engine) {
        lua_newtable(L);
        return 1;
    }
    auto modules = ProcessManager::GetModules(engine->GetProcessId());
    lua_newtable(L);
    int idx = 1;
    for (auto& mod : modules) {
        lua_newtable(L);
        lua_pushstring(L, mod.name.c_str());
        lua_setfield(L, -2, "name");
        lua_pushinteger(L, static_cast<lua_Integer>(mod.baseAddress));
        lua_setfield(L, -2, "base");
        lua_pushinteger(L, mod.size);
        lua_setfield(L, -2, "size");
        lua_rawseti(L, -2, idx++);
    }
    return 1;
}

static int lua_memforge_sleep(lua_State* L) {
    int ms = static_cast<int>(luaL_checkinteger(L, 1));
    if (ms > 0 && ms <= 60000) {
        std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    }
    return 0;
}

static int lua_memforge_print(lua_State* L) {
    auto* engine = GetEngine(L);
    if (!engine) return 0;

    int nargs = lua_gettop(L);
    std::ostringstream ss;
    for (int i = 1; i <= nargs; i++) {
        if (i > 1) ss << "\t";
        if (lua_isstring(L, i)) {
            ss << lua_tostring(L, i);
        } else if (lua_isinteger(L, i)) {
            ss << lua_tointeger(L, i);
        } else if (lua_isnumber(L, i)) {
            ss << lua_tonumber(L, i);
        } else if (lua_isboolean(L, i)) {
            ss << (lua_toboolean(L, i) ? "true" : "false");
        } else if (lua_isnil(L, i)) {
            ss << "nil";
        } else {
            ss << lua_typename(L, lua_type(L, i));
        }
    }
    ss << "\n";

    // Call output callback if set
    // We store engine pointer - need to access the callback via engine
    // The output is accumulated in the engine's output buffer
    // We'll use a global function to append
    lua_getfield(L, LUA_REGISTRYINDEX, "MemForgeOutputBuffer");
    std::string* buf = static_cast<std::string*>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    if (buf) {
        *buf += ss.str();
    }

    return 0;
}

// ── LuaEngine implementation ─────────────────────────────

LuaEngine::LuaEngine() {}

LuaEngine::~LuaEngine() {
    Shutdown();
}

void LuaEngine::Initialize(HANDLE hProcess, DWORD pid) {
    Shutdown(); // clean up any existing state

    m_hProcess = hProcess;
    m_pid = pid;

    m_luaState = luaL_newstate();
    if (!m_luaState) return;

    // Open standard libs (safe subset)
    luaL_openlibs(m_luaState);

    // Store engine pointer in registry
    lua_pushlightuserdata(m_luaState, this);
    lua_setfield(m_luaState, LUA_REGISTRYINDEX, LUAENGINE_REGISTRY_KEY);

    RegisterAPI();
}

void LuaEngine::Shutdown() {
    if (m_luaState) {
        lua_close(m_luaState);
        m_luaState = nullptr;
    }
    m_hProcess = nullptr;
    m_pid = 0;
}

void LuaEngine::RegisterAPI() {
    if (!m_luaState) return;

    lua_State* L = m_luaState;

    // Create memforge table
    lua_newtable(L);

    // Register functions
    static const luaL_Reg funcs[] = {
        {"readInt", lua_memforge_readInt},
        {"readFloat", lua_memforge_readFloat},
        {"readDouble", lua_memforge_readDouble},
        {"readString", lua_memforge_readString},
        {"readBytes", lua_memforge_readBytes},
        {"writeInt", lua_memforge_writeInt},
        {"writeFloat", lua_memforge_writeFloat},
        {"writeBytes", lua_memforge_writeBytes},
        {"getProcessId", lua_memforge_getProcessId},
        {"getModuleBase", lua_memforge_getModuleBase},
        {"getModules", lua_memforge_getModules},
        {"sleep", lua_memforge_sleep},
        {"print", lua_memforge_print},
        {nullptr, nullptr}
    };

    for (const auto* f = funcs; f->name; f++) {
        lua_pushcfunction(L, f->func);
        lua_setfield(L, -2, f->name);
    }

    lua_setglobal(L, "memforge");

    // Override global print to redirect to our console
    lua_pushcfunction(L, lua_memforge_print);
    lua_setglobal(L, "print");
}

LuaEngine::ScriptResult LuaEngine::Execute(const std::string& script) {
    ScriptResult result;
    result.success = false;

    if (!m_luaState) {
        result.error = "Lua engine not initialized";
        return result;
    }

    // Set up output buffer
    m_outputBuffer.clear();
    lua_pushlightuserdata(m_luaState, &m_outputBuffer);
    lua_setfield(m_luaState, LUA_REGISTRYINDEX, "MemForgeOutputBuffer");

    int err = luaL_loadstring(m_luaState, script.c_str());
    if (err != LUA_OK) {
        result.error = lua_tostring(m_luaState, -1);
        lua_pop(m_luaState, 1);
        return result;
    }

    err = lua_pcall(m_luaState, 0, LUA_MULTRET, 0);
    if (err != LUA_OK) {
        result.error = lua_tostring(m_luaState, -1);
        lua_pop(m_luaState, 1);
        result.output = m_outputBuffer;
        return result;
    }

    result.success = true;
    result.output = m_outputBuffer;
    return result;
}

LuaEngine::ScriptResult LuaEngine::ExecuteFile(const std::string& path) {
    ScriptResult result;
    result.success = false;

    if (!m_luaState) {
        result.error = "Lua engine not initialized";
        return result;
    }

    m_outputBuffer.clear();
    lua_pushlightuserdata(m_luaState, &m_outputBuffer);
    lua_setfield(m_luaState, LUA_REGISTRYINDEX, "MemForgeOutputBuffer");

    int err = luaL_loadfile(m_luaState, path.c_str());
    if (err != LUA_OK) {
        result.error = lua_tostring(m_luaState, -1);
        lua_pop(m_luaState, 1);
        return result;
    }

    err = lua_pcall(m_luaState, 0, LUA_MULTRET, 0);
    if (err != LUA_OK) {
        result.error = lua_tostring(m_luaState, -1);
        lua_pop(m_luaState, 1);
        result.output = m_outputBuffer;
        return result;
    }

    result.success = true;
    result.output = m_outputBuffer;
    return result;
}

} // namespace memforge
