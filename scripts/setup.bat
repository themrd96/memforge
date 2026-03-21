@echo off
REM MemForge - Setup Script
REM Downloads and sets up Dear ImGui for the project

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
echo [1/2] Downloading Dear ImGui...
if not exist "%~dp0..\libs\imgui\imgui.h" (
    cd /d "%~dp0.."
    git clone --depth 1 --branch v1.91.8 https://github.com/ocornut/imgui.git libs\imgui
    if %ERRORLEVEL% neq 0 (
        echo ERROR: Failed to clone ImGui.
        pause
        exit /b 1
    )
) else (
    echo ImGui already present, skipping.
)

REM Create build directory
echo [2/2] Creating build directory...
cd /d "%~dp0.."
if not exist build mkdir build
cd build

echo.
echo Setup complete! Now run build.bat to compile.
echo.
pause
