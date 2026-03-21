@echo off
REM MemForge - Build Script
REM Auto-detects any Visual Studio version

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

REM ─── Auto-detect Visual Studio ───
set "GENERATOR="

REM Check for VS 2026 (v18)
if exist "%ProgramFiles%\Microsoft Visual Studio\2026" (
    set "GENERATOR=Visual Studio 18 2026"
    goto :found
)
if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\2026" (
    set "GENERATOR=Visual Studio 18 2026"
    goto :found
)
if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\18" (
    set "GENERATOR=Visual Studio 18 2026"
    goto :found
)

REM Check for VS 2022 (v17)
if exist "%ProgramFiles%\Microsoft Visual Studio\2022" (
    set "GENERATOR=Visual Studio 17 2022"
    goto :found
)
if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\2022" (
    set "GENERATOR=Visual Studio 17 2022"
    goto :found
)

REM Check for VS 2019 (v16)
if exist "%ProgramFiles%\Microsoft Visual Studio\2019" (
    set "GENERATOR=Visual Studio 16 2019"
    goto :found
)
if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\2019" (
    set "GENERATOR=Visual Studio 16 2019"
    goto :found
)

REM ─── Fallback: try Ninja if 'cl' is available (Developer Command Prompt) ───
where /Q cl
if %ERRORLEVEL% equ 0 (
    echo Visual Studio directory not found in standard paths.
    echo But 'cl' compiler is available - using Ninja generator.
    set "GENERATOR=Ninja"
    goto :found_ninja
)

REM Nothing found
echo.
echo ERROR: Could not find Visual Studio or a C++ compiler.
echo.
echo Please install Visual Studio with the C++ workload,
echo or run this script from the Developer Command Prompt.
pause
exit /b 1

:found
echo Detected: %GENERATOR%
echo.

echo [1/2] Configuring with CMake...
cmake -B build -G "%GENERATOR%" -A x64
if %ERRORLEVEL% neq 0 (
    echo.
    echo Generator "%GENERATOR%" failed.
    echo Falling back to Ninja...
    where /Q cl
    if %ERRORLEVEL% equ 0 (
        goto :found_ninja
    )
    echo ERROR: CMake configuration failed.
    pause
    exit /b 1
)
goto :build

:found_ninja
echo Using Ninja generator with available compiler...
echo.
if not exist build mkdir build
echo [1/2] Configuring with CMake (Ninja)...
cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Release
if %ERRORLEVEL% neq 0 (
    echo.
    echo ERROR: CMake configuration failed.
    echo.
    echo Try running this from the Developer Command Prompt for VS.
    echo Search "Developer Command Prompt" in the Start menu.
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
