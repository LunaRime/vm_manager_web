/**
 * main.c — Entry point for VM Manager v4.0
 *
 * Modes:
 *   vm_manager.exe              → desktop GUI (default)
 *   vm_manager.exe /headless    → headless engine + HTTP dashboard only
 *   vm_manager.exe /console     → console debug mode
 *
 * Compile:
 *   gcc src/vm_*.c src/main.c -o vm_manager.exe -mwindows -lpsapi -lws2_32 -lcrypt32 -lcomctl32 -O2 -s -Wall
 */
#include "vm_common.h"

/* Forward from vm_engine.c */
void CheckAndAct(void);

/* ============================================================================
 * Dashboard HTML — embedded as a C string constant
 * ============================================================================ */
#include "dashboard_html.h"

/* ============================================================================
 * Forward from vm_desktop.c
 * ============================================================================ */
extern int RunDesktop(void);
extern const char *const DASHBOARD_HTML;

/* These are already declared in vm_common.h — just making sure linker finds them */
/* The actual definitions come from vm_desktop.c */

/* ============================================================================
 * Headless mode (no GUI, just engine + HTTP)
 * ============================================================================ */
static int RunHeadless(void) {
    const char *CLASS_NAME = "VMManagerWebMsgWindow";
    WNDCLASSA wc;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc   = DefWindowProcA;
    wc.hInstance     = GetModuleHandleA(NULL);
    wc.lpszClassName = CLASS_NAME;

    if (!RegisterClassA(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return 1;

    HWND hwnd = CreateWindowExA(0, CLASS_NAME, "VM Manager Web",
        0, 0, 0, 0, 0, HWND_MESSAGE, NULL, GetModuleHandleA(NULL), NULL);
    if (!hwnd) return 1;

    SetProcessShutdownParameters(0x100, 0);

    /* Timer-driven check cycle */
    UINT_PTR timerId = SetTimer(hwnd, 1, CHECK_INTERVAL_SEC * 1000, NULL);

    /* The timer callback uses a window procedure override approach is harder;
     * we do a simple loop instead since we own the message pump. */
    Log("VM Manager v4.0 started (headless mode)");
    Log("Dashboard: http://127.0.0.1:%d", g_httpPort);
    Log("Check interval: %d sec | Idle threshold: %d sec | PF threshold: %d%%",
        CHECK_INTERVAL_SEC, IDLE_THRESHOLD_SEC, PAGE_FILE_THRESHOLD_PCT);

    /* Initial check */
    CheckAndAct();

    /* Message loop with timer-based checks */
    MSG msg;
    DWORD nextCheck = GetTickCount() + CHECK_INTERVAL_SEC * 1000;

    while (g_bRunning) {
        /* Wait for message or timeout */
        DWORD now = GetTickCount();
        DWORD waitMs;
        if (now < nextCheck)
            waitMs = nextCheck - now;
        else
            waitMs = 0;

        DWORD result = MsgWaitForMultipleObjects(0, NULL, FALSE, waitMs, QS_ALLINPUT);

        if (result == WAIT_OBJECT_0) {
            /* There are messages to process */
            while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
                if (msg.message == WM_QUIT) {
                    g_bRunning = FALSE;
                    break;
                }
                TranslateMessage(&msg);
                DispatchMessageA(&msg);
            }
        }

        /* Check if time to run cycle */
        now = GetTickCount();
        if (now >= nextCheck) {
            CheckAndAct();
            nextCheck = now + CHECK_INTERVAL_SEC * 1000;
        }
    }

    KillTimer(hwnd, timerId);
    DestroyWindow(hwnd);

    Log("VM Manager v4.0 stopped");
    return 0;
}

/* ============================================================================
 * Console debug mode
 * ============================================================================ */
static BOOL WINAPI ConsoleCtrlHandler(DWORD fdwCtrlType) {
    switch (fdwCtrlType) {
    case CTRL_C_EVENT: case CTRL_BREAK_EVENT:
    case CTRL_CLOSE_EVENT: case CTRL_SHUTDOWN_EVENT:
        Log("Shutdown signal received, exiting...");
        g_bRunning = FALSE;
        return TRUE;
    default: return FALSE;
    }
}

static int RunConsole(void) {
    g_bConsole = TRUE;
    SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);
    SetConsoleTitleA("VM Manager v4.0 - Console Debug Mode");
    SetProcessShutdownParameters(0x100, 0);

    printf("==============================================\n");
    printf("  VM Manager v4.0\n");
    printf("  Memory Monitor + Suspicious Process Detector\n");
    printf("  Console Debug Mode\n");
    printf("==============================================\n\n");
    printf("Dashboard: http://127.0.0.1:%d\n", g_httpPort);
    printf("Check interval: %d sec\n", CHECK_INTERVAL_SEC);
    printf("Idle threshold: %d sec (%d min)\n", IDLE_THRESHOLD_SEC, IDLE_THRESHOLD_SEC / 60);
    printf("PF threshold: %d%%\n", PAGE_FILE_THRESHOLD_PCT);
    printf("Database: %s (DPAPI encrypted)\n", DB_FILE_NAME);
    printf("Press Ctrl+C to exit\n\n");

    Log("VM Manager v4.0 started (console debug mode)");

    while (g_bRunning) {
        CheckAndAct();
        int i;
        for (i = 0; i < CHECK_INTERVAL_SEC && g_bRunning; i++)
            Sleep(1000);
    }

    Log("VM Manager v4.0 stopped");
    return 0;
}

/* ============================================================================
 * WinMain
 * ============================================================================ */
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                   LPSTR lpCmdLine, int nCmdShow) {
    (void)hInstance; (void)hPrevInstance; (void)nCmdShow;

    g_tStartTime = time(NULL);

    /* MUST be first: detect language before any UI/log output */
    LocaleInit();

    /* Initialize logging */
    if (!InitLogFile()) {
        MessageBoxA(NULL,
            "Unable to create log file. Check write permissions.",
            "VM Manager v4.0 Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    Log("========================================");
    Log("VM Manager v4.0 starting");

    /* Initialize database */
    if (!InitDatabase()) {
        Log("Warning: unable to initialize encrypted database");
    } else {
        LoadDatabase();
    }

    /* Initialize synchronization */
    InitializeCriticalSection(&g_csData);

    /* Debug privilege */
    if (EnableDebugPrivilege())
        Log("SeDebugPrivilege granted");
    else
        Log("Note: SeDebugPrivilege unavailable, some trimming may fail");

    /* CPU cores */
    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);
    g_numProcessors = sysInfo.dwNumberOfProcessors;
    if (g_numProcessors < 1) g_numProcessors = 1;
    Log("CPU cores: %lu", g_numProcessors);

    /* GPU monitoring */
    InitGpuMonitoring();
    if (g_gpuInfo.available)
        Log("GPU monitoring initialized: %s", g_gpuInfo.name);
    else
        Log("GPU monitoring unavailable");

    /* Start HTTP server */
    g_hHttpThread = CreateThread(NULL, 0, HttpServerThread, NULL, 0, NULL);
    if (!g_hHttpThread)
        Log("Error: unable to start HTTP server");

    /* Wait for HTTP server */
    {
        int waitCount = 0;
        while (g_httpPort == 0 && waitCount < 30) {
            Sleep(100); waitCount++;
        }
        if (g_httpPort == 0)
            Log("Error: HTTP server init timeout");
    }

    /* Select mode */
    int result;
    if (lpCmdLine && (strstr(lpCmdLine, "/console") || strstr(lpCmdLine, "/c"))) {
        result = RunConsole();
    } else if (lpCmdLine && (strstr(lpCmdLine, "/headless") || strstr(lpCmdLine, "/h"))) {
        result = RunHeadless();
    } else {
        /* Default: desktop GUI mode */
        result = RunDesktop();
    }

    /* Cleanup */
    g_bRunning = FALSE;
    if (g_hHttpThread) {
        WaitForSingleObject(g_hHttpThread, 5000);
        CloseHandle(g_hHttpThread);
    }
    DeleteCriticalSection(&g_csData);
    ShutdownGpuMonitoring();
    if (g_hDbFile != INVALID_HANDLE_VALUE) CloseHandle(g_hDbFile);
    if (g_hLogFile != INVALID_HANDLE_VALUE) CloseHandle(g_hLogFile);

    return result;
}
