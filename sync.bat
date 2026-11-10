@echo off
echo ========================================================
echo   SimpleRISC Simulator - GitHub Auto-Sync Script
echo ========================================================
echo.
echo Pulling latest updates from GitHub...
git pull
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Could not pull from GitHub. Please check internet connection or Git status.
    pause
    exit /b %ERRORLEVEL%
)
echo.
echo [SUCCESS] Codebase is up to date with GitHub!
echo Rebuilding project...
g++ -std=c++14 -Wall -Wextra -O2 -I include src/alu.cpp src/vertical_control_unit.cpp src/horizontal_control_unit.cpp src/cpu_pipeline.cpp src/assembler.cpp src/disassembler.cpp src/stack_preprocessor.cpp src/exception_framework.cpp src/stack_visualizer.cpp src/debugger_cli.cpp src/main.cpp -o simplerisc_sim.exe
if %ERRORLEVEL% EQU 0 (
    echo [SUCCESS] Build succeeded! Executable: simplerisc_sim.exe
) else (
    echo [ERROR] Build failed.
)
echo.
pause
