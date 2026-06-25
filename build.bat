@echo off
REM ========================================================
REM  VM Manager v4.0 — Build Script
REM  Uses bundled MinGW GCC (D:/VSCode/MinGW/bin/gcc.exe)
REM ========================================================

set "GCC=..\MinGW\bin\gcc.exe"
set "SRC=src\main.c src\vm_engine.c src\vm_db.c src\vm_http.c src\vm_desktop.c"
set "OUT=vm_manager.exe"

if not exist "%GCC%" (
    echo [ERROR] GCC not found: %GCC%
    pause
    exit /b 1
)

echo [INFO] Building VM Manager v4.0...
echo [INFO] Sources: %SRC%
echo.

REM -mwindows     = no console window (GUI)
REM -lpsapi       = Process Status API
REM -lws2_32      = WinSock2 (HTTP server)
REM -lcrypt32     = DPAPI encryption
REM -lcomctl32    = Common Controls (listviews, tabs)
REM -lgdi32       = GDI (chart drawing)
REM -O2 -s        = Optimize + strip symbols
REM -Wall         = All warnings

"%GCC%" %SRC% -o %OUT% -mwindows -lpsapi -lws2_32 -lcrypt32 -lcomctl32 -lgdi32 -O2 -s -Wall

if %errorlevel% equ 0 (
    echo [SUCCESS] Build complete: %OUT%
    echo.
    echo Usage:
    echo   vm_manager.exe             — Desktop GUI (default)
    echo   vm_manager.exe /headless   — Headless engine + Web dashboard
    echo   vm_manager.exe /console    — Console debug mode
    echo.
    echo   Web Dashboard: http://127.0.0.1:18080
    echo   Log File:       vm_manager.log
    echo   Database:       vm_data.db (DPAPI encrypted)
) else (
    echo [FAILED] Build error!
)

pause
