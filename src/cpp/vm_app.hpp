/**
 * vm_app.hpp — VMApp: main application class (C++ wrapper over C core)
 *
 * Owns the complete lifecycle:
 *   1. Initialize C core (logging, DB, GPU monitoring)
 *   2. Start HTTP server for web dashboard
 *   3. Run monitoring loop (headless / console modes)
 *   4. Clean shutdown with RAII
 *
 * Default mode: silent background + web dashboard (headless).
 *
 * Usage:
 *   VMApp app;
 *   if (!app.Initialize(hInstance, lpCmdLine)) return 1;
 *   return app.Run();
 */
#ifndef VM_APP_HPP
#define VM_APP_HPP

#include "vm_bridge.hpp"
#include "vm_http_server.hpp"

#include <string>

class VMApp {
public:
    VMApp();
    ~VMApp();

    /* Non-copyable */
    VMApp(const VMApp&) = delete;
    VMApp& operator=(const VMApp&) = delete;

    /**
     * Initialize all subsystems.
     * @returns true on success, false on fatal error.
     */
    bool Initialize(HINSTANCE hInstance, LPSTR lpCmdLine);

    /**
     * Enter main loop. Blocks until shutdown.
     * @returns exit code (0 = success).
     */
    int Run();

    /**
     * Signal shutdown from any thread.
     */
    void RequestShutdown();

    /* ---- Accessors ---- */
    int    GetHttpPort()  const { return m_httpPort; }
    time_t GetStartTime() const { return m_startTime; }
    bool   IsHttpRunning() const;

    /* ---- HTTP server control ---- */
    bool StartHttpServer();
    void StopHttpServer();
    enum class Mode { Console, Headless, Desktop };

    bool ParseCommandLine(LPSTR lpCmdLine);
    bool InitCore();

    int RunConsole();
    int RunHeadless();
    int RunDesktop();

    void Cleanup();

    /* ---- Members ---- */
    HINSTANCE   m_hInstance;
    std::string m_cmdLine;
    Mode        m_mode;

    VMHttpServer m_httpServer;

    volatile LONG m_running;       /* InterlockedExchange */
    bool          m_initialized;

    int     m_httpPort;
    time_t  m_startTime;
};

/* Global singleton accessor for cross-module HTTP control */
VMApp *GetVMApp();

#endif /* VM_APP_HPP */
