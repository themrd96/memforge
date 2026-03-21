cmake_minimum_required(VERSION 3.20)
project(MemForge VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Prevent Windows.h from defining min/max macros
add_definitions(-DNOMINMAX -DWIN32_LEAN_AND_MEAN)

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

# ─── Core library (memory engine) ───
add_library(memforge_core STATIC
    src/core/process_manager.cpp
    src/core/memory_scanner.cpp
    src/core/memory_writer.cpp
    src/core/value_freezer.cpp
    src/core/stealth.cpp
)
target_include_directories(memforge_core PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(memforge_core PUBLIC psapi dwmapi)

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
