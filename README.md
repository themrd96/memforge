# MemForge

A modern memory scanner and game tool — a better Cheat Engine, built from scratch in C++ with a clean Dear ImGui interface.

![Windows](https://img.shields.io/badge/platform-Windows-0078D6?logo=windows)
![C++20](https://img.shields.io/badge/C%2B%2B-20-00599C?logo=cplusplus)
![License](https://img.shields.io/badge/license-MIT-green)

## Features

### Memory Scanner
- **Multi-threaded scanning** — uses all CPU cores for fast memory scans
- **Scan modes** — Exact value, greater/less than, between, unknown initial value, increased, decreased, changed, unchanged, increased/decreased by
- **Value types** — Byte, 2 bytes, 4 bytes, 8 bytes, float, double
- **Value freezing** — Lock any memory address to a specific value (writes 20x/sec)
- **Hex viewer** — Browse process memory with color-coded hex + ASCII display

### Speed Hack
- **Game speed control** — Speed up or slow down any game from 0.1x to 10x
- **Hooks timing functions** — QueryPerformanceCounter, GetTickCount, GetTickCount64, timeGetTime
- **DLL injection** — Automatic injection via CreateRemoteThread + LoadLibrary
- **Shared memory control** — Real-time speed adjustment without re-injection
- **Preset buttons** — Quick access to common speeds (0.25x, 0.5x, 1x, 2x, 5x, 10x)

### Modern UI
- **Dear ImGui + DirectX 11** — Hardware-accelerated, responsive UI
- **Dockable panels** — Rearrange windows however you like
- **Dark theme** — Custom dark blue/purple color scheme
- **Status bar** — Live scan progress, frozen value count, FPS

## Requirements

- **Windows 10/11** (64-bit)
- **Visual Studio 2022** with "Desktop development with C++" workload
- **CMake 3.20+** (included with Visual Studio)
- **Git** (for downloading ImGui)

## Building

### Quick Build

```batch
# 1. Clone the repo
git clone https://github.com/YOUR_USERNAME/memforge.git
cd memforge

# 2. Run setup (downloads ImGui)
scripts\setup.bat

# 3. Build
scripts\build.bat
```

Output will be in `build/bin/Release/`.

### Manual Build

```batch
# Download ImGui
git clone --depth 1 --branch v1.91.8 https://github.com/ocornut/imgui.git libs/imgui

# Configure and build
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel
```

### Visual Studio

1. Run `scripts/setup.bat` to download ImGui
2. Open the folder in Visual Studio (File → Open → Folder)
3. Visual Studio will auto-detect CMakeLists.txt
4. Select `Release` configuration and build

## Usage

> **Important:** Run MemForge.exe as Administrator. Memory operations require elevated privileges.

### Memory Scanning

1. Open MemForge and find your target game in the **Process Selector**
2. Double-click the process to attach
3. In the **Memory Scanner**, enter the value you're looking for (e.g., your current health: `100`)
4. Click **First Scan**
5. Go back to the game and change the value (take damage so health becomes `85`)
6. Enter the new value (`85`) and click **Next Scan**
7. Repeat until you have 1-3 results
8. Click **Add** to move a result to the frozen values list
9. Edit the value and toggle the freeze checkbox to lock it

### Speed Hack

1. Attach to a process
2. Open the **Speed Hack** panel
3. Check **Enable Speed Hack** (this injects the DLL)
4. Use the slider or preset buttons to adjust game speed
5. `1.0x` = normal, `2.0x` = double speed, `0.5x` = half speed

## Architecture

```
memforge/
├── include/
│   ├── core/
│   │   ├── process_manager.h    # Process enumeration and management
│   │   ├── memory_scanner.h     # Multi-threaded memory scanning engine
│   │   ├── memory_writer.h      # Memory write operations
│   │   └── value_freezer.h      # Value freezing (background write loop)
│   ├── speedhack/
│   │   └── speedhack.h          # Speed hack controller (host side)
│   └── gui/
│       └── app.h                # Main application and UI state
├── src/
│   ├── core/                    # Core engine implementations
│   ├── speedhack/
│   │   └── speedhack_dll.cpp    # Injected DLL (hooks timing functions)
│   └── gui/
│       ├── main.cpp             # Entry point
│       ├── app.cpp              # Main loop, ImGui setup, event handling
│       ├── renderer_dx11.cpp    # DirectX 11 initialization
│       ├── speedhack_controller.cpp  # DLL injection logic
│       ├── ui_process_selector.cpp   # Process list panel
│       ├── ui_scanner.cpp            # Scanner + frozen values panel
│       ├── ui_speed_hack.cpp         # Speed hack controls
│       └── ui_hex_viewer.cpp         # Hex memory viewer
└── scripts/
    ├── setup.bat                # Downloads dependencies
    └── build.bat                # Builds the project
```

## How It Works

### Memory Scanner
Uses Win32 APIs (`VirtualQueryEx`, `ReadProcessMemory`, `WriteProcessMemory`) to enumerate and scan process memory regions. Scans are multi-threaded for performance — each thread processes different memory regions in parallel.

### Speed Hack
1. Creates a shared memory region (named memory-mapped file)
2. Injects `memforge_speedhack.dll` into the target process via `CreateRemoteThread` + `LoadLibraryA`
3. The DLL patches the Import Address Table (IAT) of all loaded modules to redirect timing function calls
4. Hooked functions multiply time deltas by the speed multiplier read from shared memory
5. The main app writes speed changes to shared memory — the DLL picks them up instantly

### Value Freezer
A background thread continuously writes frozen values to their addresses at 20Hz (configurable). Uses `VirtualProtectEx` to temporarily change memory protection when writing to read-only regions.

## Disclaimer

This tool is intended for educational purposes and single-player game modification. Do not use it on online/multiplayer games — it may violate terms of service and result in bans.

## License

MIT License — see [LICENSE](LICENSE) for details.
