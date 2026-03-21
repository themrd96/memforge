@echo off
REM MemForge - Setup Script
REM Downloads and sets up Dear ImGui and Lua 5.4 for the project

echo ================================
echo  MemForge Setup
echo ================================
echo.

REM Check for git
where git >nul 2>&1
if %ERRORLEVEL% neq 0 (
    echo ERROR: Git is not installed or not in PATH.
    echo Please install Git from https://git-scm.com/
    pause
    exit /b 1
)

REM Check for CMake
where cmake >nul 2>&1
if %ERRORLEVEL% neq 0 (
    echo ERROR: CMake is not installed or not in PATH.
    echo Please install CMake from https://cmake.org/
    echo Or install it via Visual Studio Installer.
    pause
    exit /b 1
)

REM Download Dear ImGui
echo [1/3] Downloading Dear ImGui...
if not exist "%~dp0..\libs\imgui\imgui.h" (
    cd /d "%~dp0.."
    git clone --depth 1 --branch docking https://github.com/ocornut/imgui.git libs\imgui
    if %ERRORLEVEL% neq 0 (
        echo ERROR: Failed to clone ImGui.
        pause
        exit /b 1
    )
) else (
    echo ImGui already present, skipping.
)

REM Download Lua 5.4.7 source
echo [2/3] Downloading Lua 5.4.7 source...
if not exist "%~dp0..\libs\lua\src\lua.h" (
    cd /d "%~dp0.."
    if not exist libs\lua mkdir libs\lua
    echo Downloading Lua 5.4.7...
    powershell -Command "Invoke-WebRequest -Uri 'https://www.lua.org/ftp/lua-5.4.7.tar.gz' -OutFile 'libs\lua-5.4.7.tar.gz'"
    if %ERRORLEVEL% neq 0 (
        echo ERROR: Failed to download Lua source.
        echo Please download manually from https://www.lua.org/ftp/lua-5.4.7.tar.gz
        echo and extract the src/ folder to libs\lua\src\
        pause
        exit /b 1
    )
    echo Extracting Lua source...
    powershell -Command "tar -xzf 'libs\lua-5.4.7.tar.gz' -C 'libs\lua' --strip-components=1"
    if %ERRORLEVEL% neq 0 (
        REM Try alternative extraction
        powershell -Command "& { Add-Type -AssemblyName System.IO.Compression.FileSystem; [System.IO.Compression.ZipFile]::ExtractToDirectory('libs\lua-5.4.7.tar.gz', 'libs\lua') }" 2>nul
        if %ERRORLEVEL% neq 0 (
            echo WARNING: Auto-extraction failed. Please extract lua-5.4.7.tar.gz manually.
            echo Extract the src/ folder to libs\lua\src\
        )
    )
    del /q libs\lua-5.4.7.tar.gz 2>nul
) else (
    echo Lua already present, skipping.
)

REM Create build directory
echo [3/3] Creating build directory...
cd /d "%~dp0.."
if not exist build mkdir build
cd build

echo.
echo Setup complete! Now run build.bat to compile.
echo.
pause
