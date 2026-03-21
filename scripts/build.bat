@echo off
REM MemForge - Build Script
REM Requires Visual Studio 2022 with C++ workload

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

REM Configure with CMake
echo [1/2] Configuring with CMake...
cmake -B build -G "Visual Studio 17 2022" -A x64
if %ERRORLEVEL% neq 0 (
    echo.
    echo ERROR: CMake configuration failed.
    echo Make sure Visual Studio 2022 is installed with C++ workload.
    echo.
    echo You can also try with a different generator:
    echo   cmake -B build -G "Visual Studio 16 2019" -A x64
    echo   cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Release
    pause
    exit /b 1
)

REM Build
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
