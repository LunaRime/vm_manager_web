@echo off
setlocal enabledelayedexpansion
REM ============================================================
REM  VM Manager v5.0 — Build Script
REM  三层架构: C 核心 + C++ 包装 + Web 仪表盘
REM  工具链: MinGW GCC/G++ 6.3.0 (bundled at ..\MinGW\bin\)
REM  输出: vm_manager.exe (默认后台禁默运行)
REM
REM  编译策略:
REM    .c 文件 → gcc (C 编译器, 避免 C++ 名称重整)
REM    .cpp 文件 → g++ (C++ 编译器)
REM    链接 → g++ (统一链接)
REM ============================================================

set "GCC=..\MinGW\bin\gcc.exe"
set "GPP=..\MinGW\bin\g++.exe"
set "WINDRES=..\MinGW\bin\windres.exe"

set "BUILD_DIR=build"
set "OUT=vm_manager.exe"

REM ---- 编译选项 ----
set "CFLAGS=-O2 -Wall"
set "CPPFLAGS=-std=c++14 -O2 -Wall -Wno-misleading-indentation"
set "INCLUDES=-I src"

REM ---- 链接库 ----
set "LIBS=-lpsapi -lws2_32 -lcrypt32 -lcomctl32 -lgdi32 -lcomdlg32"

if not exist "%GPP%" (
    echo [ERROR] g++.exe not found: %GPP%
    echo Please ensure MinGW is at D:\VSCode\MinGW
    pause
    exit /b 1
)

echo.
echo ============================================================
echo   VM Manager v5.0 — Build Script
echo   Architecture: C Core + C++ Wrapper + Web Dashboard
echo ============================================================
echo.
echo   C Compiler: MinGW GCC 6.3.0
echo   C++ Compiler: MinGW G++ 6.3.0
echo   Output: %OUT%
echo   Default mode: Silent background + Web dashboard
echo.

REM ---- Create build directory ----
if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

echo [1/5] Compiling C core layer...
"%GCC%" -c %INCLUDES% src/core/vm_engine.c -o %BUILD_DIR%/vm_engine.o %CFLAGS%
if !errorlevel! neq 0 goto :fail
"%GCC%" -c %INCLUDES% src/core/vm_db.c    -o %BUILD_DIR%/vm_db.o    %CFLAGS%
if !errorlevel! neq 0 goto :fail
"%GCC%" -c %INCLUDES% src/core/vm_locale.c -o %BUILD_DIR%/vm_locale.o %CFLAGS%
if !errorlevel! neq 0 goto :fail
echo   [OK] 3 C modules compiled

echo [2/5] Compiling C++ wrapper layer...
"%GPP%" -c %INCLUDES% %CPPFLAGS% src/cpp/vm_i18n.cpp         -o %BUILD_DIR%/vm_i18n.o
if !errorlevel! neq 0 goto :fail
"%GPP%" -c %INCLUDES% %CPPFLAGS% src/cpp/vm_app.cpp          -o %BUILD_DIR%/vm_app.o
if !errorlevel! neq 0 goto :fail
"%GPP%" -c %INCLUDES% %CPPFLAGS% src/cpp/vm_http_server.cpp   -o %BUILD_DIR%/vm_http_server.o
if !errorlevel! neq 0 goto :fail
"%GPP%" -c %INCLUDES% %CPPFLAGS% src/cpp/vm_desktop.cpp       -o %BUILD_DIR%/vm_desktop.o
if !errorlevel! neq 0 goto :fail
echo   [OK] 4 C++ modules compiled

echo [3/5] Compiling entry point...
"%GPP%" -c %INCLUDES% %CPPFLAGS% src/main.cpp -o %BUILD_DIR%/main.o
if !errorlevel! neq 0 goto :fail
echo   [OK] main.cpp compiled

echo [4/5] Compiling icon resource...
"%WINDRES%" src/vm_manager.rc -O coff -o %BUILD_DIR%/vm_manager.res
if !errorlevel! neq 0 goto :fail
echo   [OK] vm_manager.res

echo [5/5] Linking...
"%GPP%" %BUILD_DIR%/vm_engine.o %BUILD_DIR%/vm_db.o %BUILD_DIR%/vm_locale.o ^
        %BUILD_DIR%/vm_i18n.o %BUILD_DIR%/vm_app.o ^
        %BUILD_DIR%/vm_http_server.o %BUILD_DIR%/vm_desktop.o ^
        %BUILD_DIR%/main.o %BUILD_DIR%/vm_manager.res ^
        -o %OUT% -mwindows %LIBS% -O2 -s
if !errorlevel! neq 0 goto :fail

REM ---- 显示输出 ----
for %%A in ("%OUT%") do set "FSIZE=%%~zA"
set /a FSIZE_KB=!FSIZE! / 1024

echo.
echo ============================================================
echo   Build Successful!
echo   Output: %OUT%  (!FSIZE_KB! KB)
echo ============================================================
echo.
echo Usage:
echo   vm_manager.exe              Desktop GUI + Web dashboard (DEFAULT)
echo   vm_manager.exe /headless    Background silent + Web dashboard
echo   vm_manager.exe /console     Console debug mode
echo.
echo   Web Dashboard: http://127.0.0.1:18080
echo   API Endpoints:
echo     /api/status         Current memory snapshot
echo     /api/history        24h point-in-time data
echo     /api/actions        Cleanup action log
echo     /api/cpu            Per-process CPU usage
echo     /api/gpu            GPU utilization ^& VRAM
echo     /api/anomalies      Anomaly alerts
echo     /api/suspicious     Suspicious process tracker
echo     /api/aggregated     Aggregated history
echo.

if "%~1"=="/run" (
    echo Starting VM Manager v5.0...
    start "" "%OUT%"
    echo Running in background. Open http://127.0.0.1:18080
)

goto :end

:fail
echo.
echo [FAILED] Build error! Check compiler output above.
echo.
echo Troubleshooting:
echo   1. Ensure all source files exist in src/core/ and src/cpp/
echo   2. Check that MinGW is at D:\VSCode\MinGW
echo   3. Try running: %GPP% --version
pause
exit /b 1

:end
endlocal
