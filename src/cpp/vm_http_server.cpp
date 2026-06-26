/**
 * vm_http_server.cpp — C++ HTTP server implementation
 *
 * Serves:
 *   GET /                  → embedded dashboard SPA (HTML)
 *   GET /api/status        → current memory snapshot JSON
 *   GET /api/history       → 24h point-in-time snapshots JSON
 *   GET /api/actions       → action log JSON
 *   GET /api/cpu           → per-process CPU usage JSON
 *   GET /api/gpu           → GPU info JSON
 *   GET /api/anomalies     → anomaly alerts JSON
 *   GET /api/suspicious    → suspicious process tracker JSON
 *   GET /api/aggregated?range=week → aggregated history JSON
 */
#include "vm_http_server.hpp"
#include "vm_app.hpp"

#include <cstdio>
#include <cstring>
#include <cstdlib>

/* Dashboard HTML string constant */
extern "C" {
extern const char *const DASHBOARD_HTML;
}

// ============================================================================
// Construction / Destruction
// ============================================================================

VMHttpServer::VMHttpServer()
    : m_hThread(nullptr)
    , m_listenSocket(INVALID_SOCKET)
    , m_port(0)
    , m_running(0)
{
}

VMHttpServer::~VMHttpServer()
{
    Stop();
}

// ============================================================================
// Start / Stop
// ============================================================================

bool VMHttpServer::Start(int startPort, int endPort)
{
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        Log("VMHttpServer: WSAStartup failed");
        return false;
    }

    /* Find an available port */
    for (int port = startPort; port <= endPort; port++) {
        m_listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (m_listenSocket == INVALID_SOCKET) continue;

        /* Set SO_REUSEADDR to avoid TIME_WAIT issues on restart */
        int reuse = 1;
        setsockopt(m_listenSocket, SOL_SOCKET, SO_REUSEADDR,
                   (const char *)&reuse, sizeof(reuse));

        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = inet_addr("127.0.0.1");
        addr.sin_port = htons((u_short)port);

        if (bind(m_listenSocket, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
            m_port = port;
            break;
        }

        closesocket(m_listenSocket);
        m_listenSocket = INVALID_SOCKET;
    }

    if (m_listenSocket == INVALID_SOCKET) {
        Log("VMHttpServer: all ports %d-%d occupied", startPort, endPort);
        WSACleanup();
        return false;
    }

    if (listen(m_listenSocket, SOMAXCONN) == SOCKET_ERROR) {
        Log("VMHttpServer: listen() failed");
        closesocket(m_listenSocket);
        WSACleanup();
        return false;
    }

    /* Start background thread (Windows native) */
    InterlockedExchange(&m_running, 1);
    m_hThread = CreateThread(nullptr, 0, ThreadProc, this, 0, nullptr);
    if (!m_hThread) {
        Log("VMHttpServer: CreateThread failed");
        InterlockedExchange(&m_running, 0);
        closesocket(m_listenSocket);
        WSACleanup();
        return false;
    }

    return true;
}

void VMHttpServer::Stop()
{
    InterlockedExchange(&m_running, 0);

    /* Close the listen socket to unblock accept() */
    if (m_listenSocket != INVALID_SOCKET) {
        closesocket(m_listenSocket);
        m_listenSocket = INVALID_SOCKET;
    }

    /* Wait for the server thread to exit */
    if (m_hThread) {
        WaitForSingleObject(m_hThread, 5000);
        CloseHandle(m_hThread);
        m_hThread = nullptr;
    }

    WSACleanup();
}

// ============================================================================
// Thread entry point (static)
// ============================================================================

DWORD WINAPI VMHttpServer::ThreadProc(LPVOID param)
{
    VMHttpServer *self = static_cast<VMHttpServer *>(param);
    self->ServerLoop();
    return 0;
}

// ============================================================================
// Server loop
// ============================================================================

void VMHttpServer::ServerLoop()
{
    DWORD timeout = 1000; /* 1 second accept timeout for clean shutdown */

    while (InterlockedCompareExchange(&m_running, 0, 0) != 0) {
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(m_listenSocket, &readSet);

        struct timeval tv = { 1, 0 };
        int selRet = select(0, &readSet, nullptr, nullptr, &tv);

        if (selRet <= 0) continue;
        if (InterlockedCompareExchange(&m_running, 0, 0) == 0) break;

        SOCKET client = accept(m_listenSocket, nullptr, nullptr);
        if (client == INVALID_SOCKET) continue;

        /* Set receive timeout */
        setsockopt(client, SOL_SOCKET, SO_RCVTIMEO,
                   (const char *)&timeout, sizeof(timeout));

        HandleClient(client);
        closesocket(client);
    }
}

// ============================================================================
// HTTP request handling
// ============================================================================

void VMHttpServer::HandleClient(SOCKET client)
{
    char request[8192];
    int recvLen = recv(client, request, sizeof(request) - 1, 0);
    if (recvLen <= 0) return;

    request[recvLen] = '\0';

    /* Parse request line */
    char method[16] = {0};
    char path[256] = {0};
    sscanf(request, "%15s %255s", method, path);

    if (strcmp(method, "GET") != 0) {
        Send404(client);
        return;
    }

    /* URL decode */
    std::string decoded;
    decoded.reserve(256);
    for (int i = 0; path[i] && decoded.size() < 254; i++) {
        if (path[i] == '%' && path[i + 1] && path[i + 2]) {
            char hex[3] = { path[i + 1], path[i + 2], 0 };
            decoded += (char)strtol(hex, nullptr, 16);
            i += 2;
        } else {
            decoded += path[i];
        }
    }

    RouteRequest(client, method, decoded);
}

void VMHttpServer::RouteRequest(SOCKET client,
                                  const std::string& method,
                                  const std::string& path)
{
    /* ---- Dashboard ---- */
    if (path == "/" || path == "/index.html") {
        int len = (int)strlen(DASHBOARD_HTML);
        SendResponse(client, 200, "text/html",
                     std::string(DASHBOARD_HTML, len));
        return;
    }

    /* ---- JSON API endpoints ---- */
    if (path == "/api/status") {
        SendResponse(client, 200, "application/json", BuildStatusJson());
        return;
    }
    if (path == "/api/history") {
        SendResponse(client, 200, "application/json",
                     BuildHistoryJson(24, 288));
        return;
    }
    if (path == "/api/actions") {
        SendResponse(client, 200, "application/json", BuildActionsJson());
        return;
    }
    if (path == "/api/cpu") {
        SendResponse(client, 200, "application/json", BuildCpuJson());
        return;
    }
    if (path == "/api/gpu") {
        SendResponse(client, 200, "application/json", BuildGpuJson());
        return;
    }
    if (path == "/api/anomalies") {
        SendResponse(client, 200, "application/json", BuildAnomaliesJson());
        return;
    }
    if (path == "/api/suspicious") {
        SendResponse(client, 200, "application/json", BuildSuspiciousJson());
        return;
    }
    /* ---- Server / App control ---- */
    if (path == "/api/stopserver") {
        VMApp *app = GetVMApp();
        if (app) app->StopHttpServer();
        SendResponse(client, 200, "application/json", "{\"ok\":true,\"msg\":\"Server stopped\"}");
        return;
    }
    if (path == "/api/shutdown") {
        SendResponse(client, 200, "application/json", "{\"ok\":true,\"msg\":\"Shutting down...\"}");
        /* Signal shutdown after response */
        VMApp *app = GetVMApp();
        if (app) app->RequestShutdown();
        return;
    }

    if (path.compare(0, 16, "/api/aggregated") == 0) {
        int range = CHART_WEEK;
        size_t qpos = path.find("range=");
        if (qpos != std::string::npos) {
            std::string rv = path.substr(qpos + 6);
            if (rv.find("day") == 0)   range = CHART_DAY;
            if (rv.find("week") == 0)  range = CHART_WEEK;
            if (rv.find("month") == 0) range = CHART_MONTH;
            if (rv.find("year") == 0)  range = CHART_YEAR;
        }
        SendResponse(client, 200, "application/json",
                     BuildAggregatedJson(range));
        return;
    }

    Send404(client);
}

// ============================================================================
// HTTP response helpers
// ============================================================================

void VMHttpServer::SendResponse(SOCKET client, int status,
                                  const std::string& contentType,
                                  const std::string& body)
{
    const char *statusText = (status == 200) ? "OK"
                           : (status == 404) ? "Not Found" : "Error";

    char header[512];
    int hdrLen = snprintf(header, sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s; charset=utf-8\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "\r\n",
        status, statusText, contentType.c_str(), (int)body.size());

    send(client, header, hdrLen, 0);
    if (!body.empty())
        send(client, body.c_str(), (int)body.size(), 0);
}

void VMHttpServer::Send404(SOCKET client)
{
    SendResponse(client, 404, "text/plain", "Not Found");
}

// ============================================================================
// JSON escaping
// ============================================================================

std::string VMHttpServer::JsonEscape(const char *src)
{
    std::string out;
    out.reserve(strlen(src) * 2);
    for (const char *p = src; *p; p++) {
        switch (*p) {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n";  break;
        case '\r': out += "\\r";  break;
        case '\t': out += "\\t";  break;
        default:   out += *p;     break;
        }
    }
    return out;
}

std::string VMHttpServer::FormatMB(ULONGLONG bytes)
{
    char buf[32];
    if (bytes >= 1073741824ULL)
        snprintf(buf, sizeof(buf), "%.1f GB", bytes / 1073741824.0);
    else
        snprintf(buf, sizeof(buf), "%I64u MB", (unsigned long long)(bytes / 1048576));
    return buf;
}

// ============================================================================
// JSON API: /api/status
// ============================================================================

std::string VMHttpServer::BuildStatusJson()
{
    char buf[65536];
    int len = 0;

    EnterCriticalSection(&g_csData);
    MemorySnapshot *s = &g_latestSnapshot;

    len = snprintf(buf, sizeof(buf),
        "{"
        "\"timestamp\":%I64d,"
        "\"running_seconds\":%I64d,"
        "\"page_file_pct\":%lu,"
        "\"phys_load\":%lu,"
        "\"total_phys_mb\":%I64u,"
        "\"avail_phys_mb\":%I64u,"
        "\"total_pagefile_mb\":%I64u,"
        "\"avail_pagefile_mb\":%I64u,"
        "\"idle_seconds\":%lu,"
        "\"threshold_pct\":%d,"
        "\"idle_threshold_sec\":%d,"
        "\"processes\":[",
        (long long)s->timestamp,
        (long long)(time(nullptr) - g_tStartTime),
        s->pageFilePct, s->physLoad,
        (unsigned long long)(s->totalPhys / (1024 * 1024)),
        (unsigned long long)(s->availPhys / (1024 * 1024)),
        (unsigned long long)(s->totalPageFile / (1024 * 1024)),
        (unsigned long long)(s->availPageFile / (1024 * 1024)),
        s->idleSeconds, PAGE_FILE_THRESHOLD_PCT, IDLE_THRESHOLD_SEC);

    for (int i = 0; i < s->numProcesses; i++) {
        ProcessInfo *p = &s->topProcesses[i];
        std::string esc = JsonEscape(p->name);
        len += snprintf(buf + len, sizeof(buf) - len,
            "%s{\"pid\":%lu,\"name\":\"%s\",\"commit_mb\":%I64u,\"ws_mb\":%I64u}",
            i > 0 ? "," : "",
            p->pid, esc.c_str(),
            (unsigned long long)(p->commitSize / (1024 * 1024)),
            (unsigned long long)(p->workingSet / (1024 * 1024)));
    }
    len += snprintf(buf + len, sizeof(buf) - len, "]}");
    LeaveCriticalSection(&g_csData);

    return buf;
}

// ============================================================================
// JSON API: /api/history
// ============================================================================

std::string VMHttpServer::BuildHistoryJson(int hours, int maxPoints)
{
    char buf[131072];
    int len = 0;

    EnterCriticalSection(&g_csData);
    len = snprintf(buf, sizeof(buf), "{\"points\":[");

    int total = g_snapshotCount;
    int take = maxPoints;
    if (take > total) take = total;
    int start = total - take;
    if (start < 0) start = 0;

    time_t cutoff = time(nullptr) - hours * 3600;
    int written = 0;
    for (int i = start; i < total; i++) {
        if (g_snapshots[i].timestamp < cutoff) continue;
        if (written >= take) break;
        len += snprintf(buf + len, sizeof(buf) - len,
            "%s{\"t\":%I64d,\"pf\":%lu,\"ph\":%lu}",
            written > 0 ? "," : "",
            (long long)g_snapshots[i].timestamp,
            g_snapshots[i].pageFilePct,
            g_snapshots[i].physLoad);
        written++;
    }
    len += snprintf(buf + len, sizeof(buf) - len, "]}");
    LeaveCriticalSection(&g_csData);

    return buf;
}

// ============================================================================
// JSON API: /api/actions
// ============================================================================

std::string VMHttpServer::BuildActionsJson()
{
    char buf[65536];
    int len = 0;

    EnterCriticalSection(&g_csData);
    len = snprintf(buf, sizeof(buf), "{\"actions\":[");

    for (int i = 0; i < g_actionCount; i++) {
        ActionRecord *a = &g_actions[i];
        std::string esc = JsonEscape(a->description);
        len += snprintf(buf + len, sizeof(buf) - len,
            "%s{\"t\":%I64d,\"bf\":%lu,\"af\":%lu,\"tc\":%d,\"fc\":%d,\"desc\":\"%s\"}",
            i > 0 ? "," : "",
            (long long)a->timestamp,
            a->pageFileBefore, a->pageFileAfter,
            a->trimmedCount, a->failedCount,
            esc.c_str());
    }
    len += snprintf(buf + len, sizeof(buf) - len, "]}");
    LeaveCriticalSection(&g_csData);

    return buf;
}

// ============================================================================
// JSON API: /api/cpu
// ============================================================================

std::string VMHttpServer::BuildCpuJson()
{
    char buf[32768];
    int len = snprintf(buf, sizeof(buf), "{\"processes\":[");
    int written = 0;

    for (int i = 0; i < g_cpuSampleCount && written < 30; i++) {
        CpuSample *cs = &g_cpuSamples[i];
        if (cs->cpuPercent < 1.0) continue;

        char procName[MAX_PATH] = "unknown";
        HANDLE hProcess = OpenProcess(
            PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, cs->pid);
        if (hProcess) {
            HMODULE hMod; DWORD needed;
            if (EnumProcessModules(hProcess, &hMod, sizeof(hMod), &needed))
                GetModuleBaseNameA(hProcess, hMod, procName, sizeof(procName) - 1);
            CloseHandle(hProcess);
        }

        std::string esc = JsonEscape(procName);
        len += snprintf(buf + len, sizeof(buf) - len,
            "%s{\"pid\":%lu,\"name\":\"%s\",\"cpu_pct\":%.1f,\"consecutive\":%d}",
            written > 0 ? "," : "",
            cs->pid, esc.c_str(), cs->cpuPercent, cs->consecutiveHigh);
        written++;
    }
    len += snprintf(buf + len, sizeof(buf) - len, "]}");

    return buf;
}

// ============================================================================
// JSON API: /api/gpu
// ============================================================================

std::string VMHttpServer::BuildGpuJson()
{
    char buf[4096];
    snprintf(buf, sizeof(buf),
        "{\"available\":%s,"
        "\"name\":\"%s\","
        "\"utilization\":%d,"
        "\"vram_used_mb\":%I64u,"
        "\"vram_total_mb\":%I64u,"
        "\"shared_used_mb\":%I64u,"
        "\"shared_total_mb\":%I64u}",
        g_gpuInfo.available ? "true" : "false",
        g_gpuInfo.name[0] ? g_gpuInfo.name : "N/A",
        g_gpuInfo.utilization,
        (unsigned long long)(g_gpuInfo.dedicatedUsed / (1024 * 1024)),
        (unsigned long long)(g_gpuInfo.dedicatedTotal / (1024 * 1024)),
        (unsigned long long)(g_gpuInfo.sharedUsed / (1024 * 1024)),
        (unsigned long long)(g_gpuInfo.sharedTotal / (1024 * 1024)));
    return buf;
}

// ============================================================================
// JSON API: /api/anomalies
// ============================================================================

std::string VMHttpServer::BuildAnomaliesJson()
{
    char buf[65536];
    int len = 0;

    EnterCriticalSection(&g_csData);
    len = snprintf(buf, sizeof(buf), "{\"alerts\":[");

    for (int i = 0; i < g_anomalyCount; i++) {
        AnomalyAlert *a = &g_anomalies[i];
        const char *typeStr = "unknown";
        switch (a->type) {
        case ANOMALY_CPU_HOG:     typeStr = "cpu_hog";     break;
        case ANOMALY_MEM_LEAK:    typeStr = "mem_leak";    break;
        case ANOMALY_MEM_HOG:     typeStr = "mem_hog";     break;
        case ANOMALY_GPU_HOG:     typeStr = "gpu_hog";     break;
        case ANOMALY_SUSPICIOUS:  typeStr = "suspicious";  break;
        }

        std::string escDesc = JsonEscape(a->description);
        std::string escName = JsonEscape(a->procName);
        len += snprintf(buf + len, sizeof(buf) - len,
            "%s{\"t\":%I64d,\"type\":\"%s\",\"pid\":%lu,\"name\":\"%s\","
            "\"value\":%.1f,\"commit_mb\":%I64u,\"desc\":\"%s\"}",
            i > 0 ? "," : "",
            (long long)a->timestamp, typeStr, a->pid, escName.c_str(),
            a->value, (unsigned long long)a->commitMB, escDesc.c_str());
    }
    len += snprintf(buf + len, sizeof(buf) - len, "]}");
    LeaveCriticalSection(&g_csData);

    return buf;
}

// ============================================================================
// JSON API: /api/aggregated
// ============================================================================

std::string VMHttpServer::BuildAggregatedJson(int range)
{
    char buf[32768];
    int len = 0;

    EnterCriticalSection(&g_csData);

    AggBucket *buckets = nullptr;
    int count = 0;
    const char *labelFmt = "daily";

    switch (range) {
    case CHART_DAY:
        buckets = g_hourlyBuckets;
        count = g_hourlyCount;
        labelFmt = "hourly";
        break;
    case CHART_WEEK:
        buckets = g_dailyBuckets;
        count = g_dailyCount;
        labelFmt = "daily";
        break;
    case CHART_MONTH:
        buckets = g_dailyBuckets;
        count = g_dailyCount;
        labelFmt = "daily";
        break;
    case CHART_YEAR:
        buckets = g_monthlyBuckets;
        count = g_monthlyCount;
        labelFmt = "monthly";
        break;
    default:
        buckets = g_dailyBuckets;
        count = g_dailyCount;
        break;
    }

    int maxTake = 30;
    switch (range) {
    case CHART_DAY:   maxTake = 24;  break;
    case CHART_WEEK:  maxTake = 7;   break;
    case CHART_MONTH: maxTake = 30;  break;
    case CHART_YEAR:  maxTake = 12;  break;
    }

    int take = count < maxTake ? count : maxTake;
    int start = count - take;
    if (start < 0) start = 0;

    len = snprintf(buf, sizeof(buf), "{\"range\":\"%s\",\"buckets\":[", labelFmt);
    int written = 0;
    for (int i = start; i < count; i++) {
        AggBucket *b = &buckets[i];
        double pfAvg = b->sampleCount > 0 ? b->pfSum / b->sampleCount : 0;
        double phAvg = b->sampleCount > 0 ? b->phSum / b->sampleCount : 0;
        len += snprintf(buf + len, sizeof(buf) - len,
            "%s{\"t\":%I64d,\"pf_avg\":%.1f,\"pf_max\":%.1f,"
            "\"ph_avg\":%.1f,\"ph_max\":%.1f,\"n\":%d}",
            written > 0 ? "," : "",
            (long long)b->bucketStart, pfAvg, b->pfMax,
            phAvg, b->phMax, b->sampleCount);
        written++;
    }
    len += snprintf(buf + len, sizeof(buf) - len, "]}");
    LeaveCriticalSection(&g_csData);

    return buf;
}

// ============================================================================
// JSON API: /api/suspicious   (suspicious process tracker)
// ============================================================================

std::string VMHttpServer::BuildSuspiciousJson()
{
    char buf[32768];
    int len = 0;

    EnterCriticalSection(&g_csData);
    len = snprintf(buf, sizeof(buf), "{\"processes\":[");
    int written = 0;

    for (int i = 0; i < g_suspProcCount; i++) {
        SuspiciousProc *sp = &g_suspProcs[i];
        std::string esc = JsonEscape(sp->name);
        SIZE_T growthMB = (sp->lastCommit > sp->firstCommit)
            ? ((sp->lastCommit - sp->firstCommit) / (1024 * 1024)) : 0;
        len += snprintf(buf + len, sizeof(buf) - len,
            "%s{\"pid\":%lu,\"name\":\"%s\","
            "\"first_commit_mb\":%I64u,\"last_commit_mb\":%I64u,"
            "\"growth_mb\":%I64u,\"peak_rate\":%.1f,"
            "\"first_seen\":%I64d,\"last_seen\":%I64d,"
            "\"alerts\":%d}",
            written > 0 ? "," : "",
            sp->pid, esc.c_str(),
            (unsigned long long)(sp->firstCommit / (1024 * 1024)),
            (unsigned long long)(sp->lastCommit / (1024 * 1024)),
            (unsigned long long)growthMB, sp->peakGrowthRate,
            (long long)sp->firstSeen, (long long)sp->lastSeen,
            sp->alertCount);
        written++;
    }
    len += snprintf(buf + len, sizeof(buf) - len, "]}");
    LeaveCriticalSection(&g_csData);

    return buf;
}
