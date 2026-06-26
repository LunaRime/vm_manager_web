/**
 * vm_http_server.hpp — C++ HTTP server for web dashboard
 *
 * Wraps Winsock2 TCP server in a clean C++ class.
 * Uses Windows native threads (CreateThread) for MinGW 6.3.0 compatibility.
 * Serves the embedded SPA dashboard and JSON API endpoints.
 *
 * Binds to 127.0.0.1 only (local access only, security boundary).
 */
#ifndef VM_HTTP_SERVER_HPP
#define VM_HTTP_SERVER_HPP

#include "vm_bridge.hpp"

#include <string>
#include <functional>
#include <unordered_map>

class VMHttpServer {
public:
    VMHttpServer();
    ~VMHttpServer();

    /* Non-copyable */
    VMHttpServer(const VMHttpServer&) = delete;
    VMHttpServer& operator=(const VMHttpServer&) = delete;

    /**
     * Start the HTTP server on the first available port in [startPort, endPort].
     * Spawns a background thread for accept/request handling.
     * @returns true on success.
     */
    bool Start(int startPort, int endPort);

    /**
     * Signal shutdown and wait for the server thread to exit.
     */
    void Stop();

    /** @returns the port the server is listening on, or 0 if not started. */
    int GetPort() const { return m_port; }

    /** @returns the raw thread handle (for legacy C code). */
    HANDLE GetThreadHandle() const { return m_hThread; }

private:
    /* ---- Server thread (static, called via CreateThread) ---- */
    static DWORD WINAPI ThreadProc(LPVOID param);
    void ServerLoop();
    void HandleClient(SOCKET client);

    /* ---- HTTP helpers ---- */
    void SendResponse(SOCKET client, int status,
                      const std::string& contentType,
                      const std::string& body);
    void Send404(SOCKET client);
    void RouteRequest(SOCKET client,
                      const std::string& method,
                      const std::string& path);

    /* ---- JSON API generators ---- */
    std::string BuildStatusJson();
    std::string BuildHistoryJson(int hours, int maxPoints);
    std::string BuildActionsJson();
    std::string BuildCpuJson();
    std::string BuildGpuJson();
    std::string BuildAnomaliesJson();
    std::string BuildAggregatedJson(int range);
    std::string BuildSuspiciousJson();

    /* ---- JSON escaping ---- */
    static std::string JsonEscape(const char *src);
    static std::string FormatMB(ULONGLONG bytes);

    /* ---- Members ---- */
    HANDLE          m_hThread;       /* Windows native thread handle */
    SOCKET          m_listenSocket;
    int             m_port;
    volatile LONG   m_running;       /* InterlockedExchange for atomic access */
};

#endif /* VM_HTTP_SERVER_HPP */
