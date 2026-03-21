cmake_minimum_required(VERSION 3.20)
project(MemForge VERSION 1.0.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Prevent Windows.h from defining min/max macros
# Suppress MSVC CRT deprecation warnings for strncpy etc.
add_definitions(-DNOMINMAX -DWIN32_LEAN_AND_MEAN -D_CRT_SECURE_NO_WARNINGS)

# ─── Options ───
option(MEMFORGE_BUILD_SPEEDHACK "Build speedhack DLL" ON)

# ─── ImGui (bundled) ───
set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/libs/imgui)
add_library(imgui STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
    ${IMGUI_DIR}/backends/imgui_impl_dx11.cpp
)
target_include_directories(imgui PUBLIC ${IMGUI_DIR} ${IMGUI_DIR}/backends)
target_link_libraries(imgui PUBLIC d3d11 d3dcompiler dxgi)

# ─── Lua 5.4 (bundled as source) ───
set(LUA_DIR ${CMAKE_SOURCE_DIR}/libs/lua/src)
add_library(lua54 STATIC
    ${LUA_DIR}/lapi.c
    ${LUA_DIR}/lauxlib.c
    ${LUA_DIR}/lbaselib.c
    ${LUA_DIR}/lcode.c
    ${LUA_DIR}/lcorolib.c
    ${LUA_DIR}/lctype.c
    ${LUA_DIR}/ldblib.c
    ${LUA_DIR}/ldebug.c
    ${LUA_DIR}/ldo.c
    ${LUA_DIR}/ldump.c
    ${LUA_DIR}/lfunc.c
    ${LUA_DIR}/lgc.c
    ${LUA_DIR}/linit.c
    ${LUA_DIR}/liolib.c
    ${LUA_DIR}/llex.c
    ${LUA_DIR}/lmathlib.c
    ${LUA_DIR}/lmem.c
    ${LUA_DIR}/loadlib.c
    ${LUA_DIR}/lobject.c
    ${LUA_DIR}/lopcodes.c
    ${LUA_DIR}/loslib.c
    ${LUA_DIR}/lparser.c
    ${LUA_DIR}/lstate.c
    ${LUA_DIR}/lstring.c
    ${LUA_DIR}/lstrlib.c
    ${LUA_DIR}/ltable.c
    ${LUA_DIR}/ltablib.c
    ${LUA_DIR}/ltm.c
    ${LUA_DIR}/lundump.c
    ${LUA_DIR}/lutf8lib.c
    ${LUA_DIR}/lvm.c
    ${LUA_DIR}/lzio.c
)
target_include_directories(lua54 PUBLIC ${LUA_DIR})
# Suppress MSVC warnings for Lua C code
if(MSVC)
    target_compile_options(lua54 PRIVATE /W0)
endif()

# ─── Core library (memory engine) ───
add_library(memforge_core STATIC
    src/core/process_manager.cpp
    src/core/memory_scanner.cpp
    src/core/memory_writer.cpp
    src/core/value_freezer.cpp
    src/core/stealth.cpp
    src/core/structure_dissector.cpp
    src/core/pointer_scanner.cpp
    src/core/lua_engine.cpp
    src/core/cheat_table.cpp
    src/core/engine_detector.cpp
    src/core/packet_inspector.cpp
    src/core/aob_scanner.cpp
    src/core/hotkey_manager.cpp
    src/core/trainer_builder.cpp
    src/core/memory_snapshot.cpp
    src/core/undo_history.cpp
    src/core/anti_anticheat.cpp
)
target_include_directories(memforge_core PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(memforge_core PUBLIC psapi dwmapi lua54 iphlpapi ws2_32)

# ─── Speedhack DLL (injected into target) ───
if(MEMFORGE_BUILD_SPEEDHACK)
    add_library(memforge_speedhack SHARED
        src/speedhack/speedhack_dll.cpp
    )
    target_include_directories(memforge_speedhack PRIVATE ${CMAKE_SOURCE_DIR}/include)
    set_target_properties(memforge_speedhack PROPERTIES
        OUTPUT_NAME "memforge_speedhack"
        PREFIX ""
    )
endif()

# ─── Main GUI application ───
add_executable(MemForge WIN32
    src/gui/main.cpp
    src/gui/app.cpp
    src/gui/renderer_dx11.cpp
    src/gui/speedhack_controller.cpp
    src/gui/ui_process_selector.cpp
    src/gui/ui_scanner.cpp
    src/gui/ui_speed_hack.cpp
    src/gui/ui_hex_viewer.cpp
    src/gui/ui_stealth.cpp
    src/gui/ui_structure_dissector.cpp
    src/gui/ui_pointer_scanner.cpp
    src/gui/ui_script_editor.cpp
    src/gui/ui_network.cpp
    src/gui/ui_aob_scanner.cpp
    src/gui/ui_hotkeys.cpp
    src/gui/ui_trainer_builder.cpp
    src/gui/ui_snapshots.cpp
    src/gui/ui_undo_history.cpp
    src/gui/ui_anticheat.cpp
)
target_include_directories(MemForge PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(MemForge PRIVATE memforge_core imgui dwmapi)

# Copy speedhack DLL next to exe
if(MEMFORGE_BUILD_SPEEDHACK)
    add_custom_command(TARGET MemForge POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:memforge_speedhack>
            $<TARGET_FILE_DIR:MemForge>
    )
endif()
