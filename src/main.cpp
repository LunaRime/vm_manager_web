/**
 * main.cpp — VM Manager v5.0 入口点
 *
 * 三层架构:
 *   Layer 1: C 底层核心后端 (src/core/)    — 内存监控、DPAPI加密、Win32 API
 *   Layer 2: C++ 包装层 (src/cpp/)        — RAII生命周期、面向对象封装、桌面GUI
 *   Layer 3: Web 前端 (src/web/)          — 内嵌仪表盘 SPA, REST API
 *
 * 默认模式: 后台禁默运行 + Web 仪表盘 (http://127.0.0.1:18080)
 *
 * 运行模式:
 *   vm_manager.exe              → 桌面 GUI + Web 仪表盘 (默认)
 *   vm_manager.exe /headless    → 后台禁默 + Web 仪表盘
 *   vm_manager.exe /console     → 控制台调试模式
 *
 * 编译:
 *   gcc -c src/core/vm_*.c                          (C核心 → .o)
 *   g++ -std=c++14 -c src/cpp/vm_*.cpp src/main.cpp (C++包装 → .o)
 *   g++ *.o -o vm_manager.exe -mwindows ...         (链接)
 */
#include "cpp/vm_app.hpp"

// ============================================================================
// WinMain — Windows GUI 入口 (无控制台窗口 / 禁默后台)
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
// main — 控制台入口 (备用, 用于 /console 调试编译)
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
