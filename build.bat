@echo off
setlocal enabledelayedexpansion
REM ============================================================
REM  VM Manager v4.0 — One-Click Build & Run
REM  Uses bundled MinGW GCC (..\MinGW\bin\gcc.exe)
REM  Output: vm_manager.exe
REM ============================================================

set "GCC=..\MinGW\bin\gcc.exe"
set "SRC=src\main.c src\vm_engine.c src\vm_db.c src\vm_http.c src\vm_desktop.c"
set "OUT=vm_manager.exe"
set "CFLAGS=-mwindows -lpsapi -lws2_32 -lcrypt32 -lcomctl32 -lgdi32"
set "OPT=-O2 -s -Wall"

if not exist "%GCC%" (
    echo [ERROR] GCC not found: %GCC%
    echo Please ensure MinGW is at D:\VSCode\MinGW
    pause
    exit /b 1
)

echo.
echo ============================================================
echo   VM Manager v4.0 — Build Script
echo ============================================================
echo.
echo   Compiler:  MinGW GCC 6.3.0
echo   Sources:   5 modules (engine + db + http + desktop + main)
echo   Output:    %OUT%
echo.

echo [1/2] Compiling...
"%GCC%" %SRC% -o %OUT% %CFLAGS% %OPT%

if !errorlevel! neq 0 (
    echo.
    echo [FAILED] Build error! Check compiler output above.
    pause
    exit /b 1
)

echo [2/2] Build complete!
echo.
echo ============================================================
echo   Build Successful
echo   Output: %OUT%
echo   Size:   %~z0 bytes
echo ============================================================
echo.
echo Usage:
echo   vm_manager.exe              Desktop GUI (default, recommended^)
echo   vm_manager.exe /headless     Headless engine + Web dashboard
echo   vm_manager.exe /console      Console debug mode
echo.
echo   Web Dashboard: http://127.0.0.1:18080
echo   Tray icon:      right-click to show/cleanup/exit
echo   Close window:   minimizes to tray (doesn't quit^)
echo.

if "%~1"=="/run" (
    echo Starting VM Manager v4.0...
    start "" "%OUT%"
    echo Running in background. Check system tray.
)

pause
endlocal
