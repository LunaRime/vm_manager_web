/**
 * main.cpp — VM Manager v5.0 入口点
 *
 * 三层架构:
 *   Layer 1: C 底层核心后端 (src/core/)    — 内存监控、DPAPI加密、Win32 API
 *   Layer 2: C++ 包装层 (src/cpp/)        — RAII生命周期、面向对象封装
 *   Layer 3: Web 前端 (src/web/)          — 内嵌仪表盘 SPA, REST API
 *
 * 默认模式: 后台禁默运行 + Web 仪表盘 (http://127.0.0.1:18080)
 *
 * 运行模式:
 *   vm_manager.exe              → 后台禁默 (默认)
 *   vm_manager.exe /console     → 控制台调试
 *   vm_manager.exe /desktop     → 桌面 GUI
 *
 * 编译:
 *   g++ src/core/vm_*.c src/cpp/vm_*.cpp src/main.cpp -o vm_manager.exe
 *       -mwindows -lpsapi -lws2_32 -lcrypt32 -lcomctl32 -lgdi32
 *       -O2 -s -Wall -std=c++14
 */
#include "cpp/vm_app.hpp"

/**
 * 桌面 GUI 模块（可选编译）
 * 如需桌面 GUI，在编译时链接 vm_desktop.c 并定义 WITH_DESKTOP_GUI
 */
#ifdef WITH_DESKTOP_GUI
extern "C" {
int RunDesktop(void);
}
#endif

// ============================================================================
// WinMain — Windows GUI 入口 (无控制台窗口)
// ============================================================================
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                   LPSTR lpCmdLine, int nCmdShow)
{
    (void)hPrevInstance;
    (void)nCmdShow;

    VMApp app;

    if (!app.Initialize(hInstance, lpCmdLine)) {
        return 1;
    }

    return app.Run();
}

// ============================================================================
// main — 控制台入口 (备用, 用于调试编译)
// ============================================================================
#ifdef _CONSOLE
int main(int argc, char *argv[])
{
    LPSTR cmdLine = GetCommandLineA();

    VMApp app;

    if (!app.Initialize(GetModuleHandleA(nullptr), cmdLine)) {
        return 1;
    }

    return app.Run();
}
#endif
