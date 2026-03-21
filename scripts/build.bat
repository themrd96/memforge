@echo off
REM MemForge - Build Script
REM Auto-detects Visual Studio version

echo ================================
echo  MemForge Build
echo ================================
echo.

cd /d "%~dp0.."

REM Run setup if ImGui not present
if not exist "libs\imgui\imgui.h" (
    echo ImGui not found, running setup first...
    call scripts\setup.bat
)

REM ─── Auto-detect Visual Studio version ───
set "GENERATOR="

REM Check for VS 2022
where /Q cl 2>nul
cmake -G "Visual Studio 17 2022" -A x64 --check-system-vars >nul 2>&1
if %ERRORLEVEL% equ 0 (
    set "GENERATOR=Visual Studio 17 2022"
    goto :found
)

REM Check common VS 2022 paths
if exist "%ProgramFiles%\Microsoft Visual Studio\2022" (
    set "GENERATOR=Visual Studio 17 2022"
    goto :found
)
if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\2022" (
    set "GENERATOR=Visual Studio 17 2022"
    goto :found
)

REM Check for VS 2019
if exist "%ProgramFiles%\Microsoft Visual Studio\2019" (
    set "GENERATOR=Visual Studio 16 2019"
    goto :found
)
if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\2019" (
    set "GENERATOR=Visual Studio 16 2019"
    goto :found
)

REM Check for VS Build Tools 2022
if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools" (
    set "GENERATOR=Visual Studio 17 2022"
    goto :found
)

REM Check for VS Build Tools 2019
if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\2019\BuildTools" (
    set "GENERATOR=Visual Studio 16 2019"
    goto :found
)

REM Try Ninja as fallback (works with any compiler)
where /Q ninja 2>nul
if %ERRORLEVEL% equ 0 (
    echo No Visual Studio found, trying Ninja...
    set "GENERATOR=Ninja"
    goto :found_ninja
)

REM Nothing found
echo.
echo ERROR: Could not find Visual Studio or a C++ compiler.
echo.
echo Please install one of these (free):
echo   1. Visual Studio 2022 Community: https://visualstudio.microsoft.com/vs/community/
echo      - Select "Desktop development with C++" workload
echo   2. Visual Studio 2019 Community (if you prefer older)
echo   3. Visual Studio Build Tools: https://visualstudio.microsoft.com/visual-cpp-build-tools/
echo.
echo After installing, run this script again.
pause
exit /b 1

:found
echo Detected: %GENERATOR%
echo.

echo [1/2] Configuring with CMake...
cmake -B build -G "%GENERATOR%" -A x64
if %ERRORLEVEL% neq 0 (
    echo.
    echo CMake configuration failed with %GENERATOR%.
    echo Trying without specifying architecture...
    cmake -B build -G "%GENERATOR%"
    if %ERRORLEVEL% neq 0 (
        echo.
        echo ERROR: CMake configuration failed.
        pause
        exit /b 1
    )
)
goto :build

:found_ninja
echo [1/2] Configuring with Ninja...
cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Release
if %ERRORLEVEL% neq 0 (
    echo ERROR: CMake configuration failed.
    pause
    exit /b 1
)
goto :build

:build
echo [2/2] Building Release...
cmake --build build --config Release --parallel
if %ERRORLEVEL% neq 0 (
    echo.
    echo ERROR: Build failed. Check the errors above.
    pause
    exit /b 1
)

echo.
echo ================================
echo  Build successful!
echo  Output: build\bin\Release\
echo ================================
echo.
echo Files:
echo   MemForge.exe           - Main application
echo   memforge_speedhack.dll - Speed hack module
echo.
echo Run MemForge.exe as Administrator for full functionality.
echo.
pause
