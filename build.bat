@echo off
:: ─────────────────────────────────────────────────────────────
::  build.bat  –  Warehouse Humanoid Robot Sim build script
::  Uses Visual Studio 2022 Community + bundled CMake
:: ─────────────────────────────────────────────────────────────
setlocal

set VSDEV="C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"
set BUILD_DIR=%~dp0build

echo [BUILD] Initialising Visual Studio environment...
call %VSDEV% -arch=x64 -host_arch=x64

if errorlevel 1 (
    echo [ERROR] Could not initialise VS environment.
    pause & exit /b 1
)

echo [BUILD] Creating build directory: %BUILD_DIR%
if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

echo [BUILD] Running CMake configure...
cmake -S "%~dp0" -B "%BUILD_DIR%" ^
      -G "NMake Makefiles" ^
      -DCMAKE_BUILD_TYPE=Release ^
      -DCMAKE_CXX_COMPILER=cl

if errorlevel 1 (
    echo [ERROR] CMake configure failed.
    pause & exit /b 1
)

echo [BUILD] Compiling...
cmake --build "%BUILD_DIR%" --config Release -- -j%NUMBER_OF_PROCESSORS%

if errorlevel 1 (
    echo [ERROR] Build failed.
    pause & exit /b 1
)

echo.
echo [BUILD] SUCCESS!
echo  Executable : %BUILD_DIR%\robot_sim.exe
echo  To run     : cd /d "%BUILD_DIR%" ^&^& robot_sim.exe
echo.

:: Launch
echo [LAUNCH] Starting simulation...
cd /d "%BUILD_DIR%"
robot_sim.exe

endlocal
