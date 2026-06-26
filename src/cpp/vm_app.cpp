/**
 * vm_app.cpp — VMApp implementation
 *
 * C++ wrapper over C core engine. Manages lifecycle with RAII.
 * Default mode: silent background + web dashboard.
 */
#include "vm_app.hpp"
#include "vm_desktop.hpp"
#include <cstdio>
#include <cstring>
#include <cstdlib>

/* Include dashboard HTML (defines DASHBOARD_HTML string) */
extern "C" {
#include "../web/dashboard_html.h"
}

// ============================================================================
// Construction / Destruction
// ============================================================================

VMApp::VMApp()
    : m_hInstance(nullptr)
    , m_mode(Mode::Headless)
    , m_running(0)
    , m_initialized(false)
    , m_httpPort(0)
    , m_startTime(0)
{
}

VMApp::~VMApp()
{
    Cleanup();
}

// ============================================================================
// Initialization
// ============================================================================

bool VMApp::Initialize(HINSTANCE hInstance, LPSTR lpCmdLine)
{
    m_hInstance = hInstance;
    if (lpCmdLine) m_cmdLine = lpCmdLine;

    ParseCommandLine(lpCmdLine);

    if (!InitCore()) return false;
    if (!StartHttpServer()) return false;

    m_initialized = true;
    return true;
}

bool VMApp::ParseCommandLine(LPSTR lpCmdLine)
{
    m_mode = Mode::Headless;  /* default: silent background */

    if (!lpCmdLine || lpCmdLine[0] == '\0') return true;

    if (strstr(lpCmdLine, "/console") || strstr(lpCmdLine, "/c"))
        m_mode = Mode::Console;
    else if (strstr(lpCmdLine, "/desktop") || strstr(lpCmdLine, "/gui"))
        m_mode = Mode::Desktop;

    return true;
}

bool VMApp::InitCore()
{
    m_startTime = time(nullptr);

    /* 1. Locale detection (before any UI or log output) */
    LocaleInit();

    /* 2. Logging */
    if (!InitLogFile()) {
        MessageBoxA(nullptr,
            "Unable to create log file. Check write permissions.",
            "VM Manager v5.0 Error", MB_OK | MB_ICONERROR);
        return false;
    }

    Log("========================================");
    Log("VM Manager v5.0 starting");
    Log("Architecture: C Core + C++ Wrapper + Web Dashboard");

    /* 3. Database */
    if (!InitDatabase()) {
        Log("Warning: unable to initialize encrypted database");
    } else {
        LoadDatabase();
    }

    /* 4. Synchronization */
    InitializeCriticalSection(&g_csData);

    /* 5. Debug privilege */
    if (EnableDebugPrivilege())
        Log("SeDebugPrivilege granted");
    else
        Log("Note: SeDebugPrivilege unavailable, some trimming may fail");

    /* 6. CPU info */
    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);
    g_numProcessors = sysInfo.dwNumberOfProcessors;
    if (g_numProcessors < 1) g_numProcessors = 1;
    Log("CPU cores: %lu", g_numProcessors);

    /* 7. GPU monitoring */
    InitGpuMonitoring();
    if (g_gpuInfo.available)
        Log("GPU monitoring initialized: %s", g_gpuInfo.name);
    else
        Log("GPU monitoring unavailable");

    return true;
}

bool VMApp::StartHttpServer()
{
    if (!m_httpServer.Start(HTTP_PORT_START, HTTP_PORT_END)) {
        Log("Error: unable to start HTTP server");
        return false;
    }

    m_httpPort = m_httpServer.GetPort();
    Log("Dashboard started: http://127.0.0.1:%d", m_httpPort);

    /* Sync the C global (used by legacy code) */
    g_httpPort = m_httpPort;
    g_hHttpThread = m_httpServer.GetThreadHandle();

    return true;
}

// ============================================================================
// Run modes
// ============================================================================

int VMApp::Run()
{
    if (!m_initialized) return 1;

    InterlockedExchange(&m_running, 1);

    switch (m_mode) {
    case Mode::Console:  return RunConsole();
    case Mode::Headless: return RunHeadless();
    case Mode::Desktop:  return RunDesktop();
    default:             return RunHeadless();
    }
}

void VMApp::RequestShutdown()
{
    InterlockedExchange(&m_running, 0);
    g_bRunning = FALSE;
}

// ============================================================================
// Console mode — visible debug output
// ============================================================================

static BOOL WINAPI ConsoleCtrlHandler(DWORD fdwCtrlType)
{
    switch (fdwCtrlType) {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
    case CTRL_CLOSE_EVENT:
    case CTRL_SHUTDOWN_EVENT:
        Log("Shutdown signal received, exiting...");
        g_bRunning = FALSE;
        return TRUE;
    default:
        return FALSE;
    }
}

int VMApp::RunConsole()
{
    g_bConsole = TRUE;
    SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);
    SetConsoleTitleA("VM Manager v5.0 - Console Debug Mode");
    SetProcessShutdownParameters(0x100, 0);

    printf("==============================================\n");
    printf("  VM Manager v5.0\n");
    printf("  Architecture: C Core + C++ Wrapper + Web\n");
    printf("  Console Debug Mode\n");
    printf("==============================================\n\n");
    printf("Dashboard: http://127.0.0.1:%d\n", m_httpPort);
    printf("Check interval: %d sec\n", CHECK_INTERVAL_SEC);
    printf("Idle threshold: %d sec (%d min)\n",
           IDLE_THRESHOLD_SEC, IDLE_THRESHOLD_SEC / 60);
    printf("PF threshold: %d%%\n", PAGE_FILE_THRESHOLD_PCT);
    printf("Database: %s (DPAPI encrypted)\n", DB_FILE_NAME);
    printf("Press Ctrl+C to exit\n\n");

    Log("VM Manager v5.0 started (console debug mode)");

    /* Initial snapshot */
    CheckAndAct();

    while (InterlockedCompareExchange(&m_running, 0, 0) != 0 && g_bRunning) {
        CheckAndAct();
        for (int i = 0; i < CHECK_INTERVAL_SEC; i++) {
            if (InterlockedCompareExchange(&m_running, 0, 0) == 0 || !g_bRunning)
                break;
            Sleep(1000);
        }
    }

    Log("VM Manager v5.0 stopped");
    return 0;
}

// ============================================================================
// Headless mode — silent background (default)
// ============================================================================

int VMApp::RunHeadless()
{
    /* Create a message-only window for message pump */
    const char *CLASS_NAME = "VMManagerCppMsgWnd";
    WNDCLASSA wc;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc   = DefWindowProcA;
    wc.hInstance     = m_hInstance;
    wc.lpszClassName = CLASS_NAME;

    if (!RegisterClassA(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return 1;

    HWND hwnd = CreateWindowExA(0, CLASS_NAME, "VM Manager v5.0",
        0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, m_hInstance, nullptr);
    if (!hwnd) return 1;

    SetProcessShutdownParameters(0x100, 0);

    Log("VM Manager v5.0 started (headless / silent background mode)");
    Log("Dashboard: http://127.0.0.1:%d", m_httpPort);
    Log("Check interval: %d sec | Idle threshold: %d sec | PF threshold: %d%%",
        CHECK_INTERVAL_SEC, IDLE_THRESHOLD_SEC, PAGE_FILE_THRESHOLD_PCT);

    /* Initial check */
    CheckAndAct();

    /* Message loop with timer-based monitoring */
    MSG msg;
    DWORD nextCheck = GetTickCount() + CHECK_INTERVAL_SEC * 1000;

    while (InterlockedCompareExchange(&m_running, 0, 0) != 0 && g_bRunning) {
        DWORD now = GetTickCount();
        DWORD waitMs = (now < nextCheck) ? (nextCheck - now) : 0;

        DWORD result = MsgWaitForMultipleObjects(0, nullptr, FALSE,
                                                   waitMs, QS_ALLINPUT);

        if (result == WAIT_OBJECT_0) {
            while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
                if (msg.message == WM_QUIT) {
                    RequestShutdown();
                    break;
                }
                TranslateMessage(&msg);
                DispatchMessageA(&msg);
            }
        }

        now = GetTickCount();
        if (now >= nextCheck) {
            CheckAndAct();
            nextCheck = now + CHECK_INTERVAL_SEC * 1000;
        }
    }

    DestroyWindow(hwnd);
    Log("VM Manager v5.0 stopped");
    return 0;
}

// ============================================================================
// Desktop mode — full C++ GUI with i18n, database panel, GDI charts
// ============================================================================

int VMApp::RunDesktop()
{
    Log("VM Manager v5.0 starting in desktop GUI mode");

    g_bDesktop = TRUE;
    InterlockedExchange(&m_running, 1);

    VMDesktopApp desktop;
    return desktop.Run(m_hInstance, SW_SHOW);
}

// ============================================================================
// Cleanup (RAII)
// ============================================================================

void VMApp::Cleanup()
{
    RequestShutdown();

    /* Stop HTTP server */
    m_httpServer.Stop();

    /* Wait for legacy HTTP thread if external */
    if (g_hHttpThread) {
        WaitForSingleObject(g_hHttpThread, 5000);
        CloseHandle(g_hHttpThread);
        g_hHttpThread = nullptr;
    }

    /* GPU monitoring */
    ShutdownGpuMonitoring();

    /* Critical section */
    DeleteCriticalSection(&g_csData);

    /* Database & log */
    if (g_hDbFile != INVALID_HANDLE_VALUE) {
        CloseHandle(g_hDbFile);
        g_hDbFile = INVALID_HANDLE_VALUE;
    }
    if (g_hLogFile != INVALID_HANDLE_VALUE) {
        CloseHandle(g_hLogFile);
        g_hLogFile = INVALID_HANDLE_VALUE;
    }
}
