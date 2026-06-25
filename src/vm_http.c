/**
 * vm_http.c — HTTP server with embedded dashboard SPA.
 *             Shares state via g_csData with engine.
 */
#include "vm_common.h"

/* ============================================================================
 * JSON helpers
 * ============================================================================ */
static void JsonEscape(const char *src, char *dst, int dstSize) {
    int j = 0, i;
    for (i = 0; src[i] && j < dstSize - 2; i++) {
        switch (src[i]) {
        case '"':  dst[j++] = '\\'; dst[j++] = '"'; break;
        case '\\': dst[j++] = '\\'; dst[j++] = '\\'; break;
        case '\n': dst[j++] = '\\'; dst[j++] = 'n'; break;
        case '\r': dst[j++] = '\\'; dst[j++] = 'r'; break;
        case '\t': dst[j++] = '\\'; dst[j++] = 't'; break;
        default:   dst[j++] = src[i]; break;
        }
    }
    dst[j] = '\0';
}

/* ============================================================================
 * JSON response writers
 * ============================================================================ */
static void WriteJsonStatus(char *buf, int bufSize, int *len) {
    EnterCriticalSection(&g_csData);
    MemorySnapshot *s = &g_latestSnapshot;
    *len = snprintf(buf, bufSize,
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
        (long long)(time(NULL) - g_tStartTime),
        s->pageFilePct, s->physLoad,
        (unsigned long long)(s->totalPhys / (1024 * 1024)),
        (unsigned long long)(s->availPhys / (1024 * 1024)),
        (unsigned long long)(s->totalPageFile / (1024 * 1024)),
        (unsigned long long)(s->availPageFile / (1024 * 1024)),
        s->idleSeconds, PAGE_FILE_THRESHOLD_PCT, IDLE_THRESHOLD_SEC);

    int i;
    for (i = 0; i < s->numProcesses; i++) {
        ProcessInfo *p = &s->topProcesses[i];
        char escaped[MAX_PATH * 2];
        JsonEscape(p->name, escaped, sizeof(escaped));
        *len += snprintf(buf + *len, bufSize - *len,
            "%s{\"pid\":%lu,\"name\":\"%s\",\"commit_mb\":%I64u,\"ws_mb\":%I64u}",
            i > 0 ? "," : "",
            p->pid, escaped,
            (unsigned long long)(p->commitSize / (1024 * 1024)),
            (unsigned long long)(p->workingSet / (1024 * 1024)));
    }
    *len += snprintf(buf + *len, bufSize - *len, "]}");
    LeaveCriticalSection(&g_csData);
}

static void WriteJsonHistory(char *buf, int bufSize, int *len,
                             int hours, int maxPoints) {
    EnterCriticalSection(&g_csData);
    *len = snprintf(buf, bufSize, "{\"points\":[");

    int total = g_snapshotCount;
    int take = maxPoints;
    if (take > total) take = total;
    int start = total - take;
    if (start < 0) start = 0;

    time_t cutoff = time(NULL) - hours * 3600;
    int written = 0, i;
    for (i = start; i < total; i++) {
        if (g_snapshots[i].timestamp < cutoff) continue;
        if (written >= take) break;
        *len += snprintf(buf + *len, bufSize - *len,
            "%s{\"t\":%I64d,\"pf\":%lu,\"ph\":%lu}",
            written > 0 ? "," : "",
            (long long)g_snapshots[i].timestamp,
            g_snapshots[i].pageFilePct, g_snapshots[i].physLoad);
        written++;
    }
    *len += snprintf(buf + *len, bufSize - *len, "]}");
    LeaveCriticalSection(&g_csData);
}

static void WriteJsonActions(char *buf, int bufSize, int *len) {
    EnterCriticalSection(&g_csData);
    *len = snprintf(buf, bufSize, "{\"actions\":[");
    int i;
    for (i = 0; i < g_actionCount; i++) {
        ActionRecord *a = &g_actions[i];
        char escaped[512];
        JsonEscape(a->description, escaped, sizeof(escaped));
        *len += snprintf(buf + *len, bufSize - *len,
            "%s{\"t\":%I64d,\"bf\":%lu,\"af\":%lu,\"tc\":%d,\"fc\":%d,\"desc\":\"%s\"}",
            i > 0 ? "," : "",
            (long long)a->timestamp,
            a->pageFileBefore, a->pageFileAfter,
            a->trimmedCount, a->failedCount, escaped);
    }
    *len += snprintf(buf + *len, bufSize - *len, "]}");
    LeaveCriticalSection(&g_csData);
}

static void WriteJsonCpu(char *buf, int bufSize, int *len) {
    *len = snprintf(buf, bufSize, "{\"processes\":[");
    int written = 0, i;
    for (i = 0; i < g_cpuSampleCount && written < 30; i++) {
        CpuSample *cs = &g_cpuSamples[i];
        if (cs->cpuPercent < 1.0) continue;

        char procName[MAX_PATH] = "unknown";
        HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ,
                                      FALSE, cs->pid);
        if (hProcess) {
            HMODULE hMod; DWORD needed;
            if (EnumProcessModules(hProcess, &hMod, sizeof(hMod), &needed))
                GetModuleBaseNameA(hProcess, hMod, procName, sizeof(procName) - 1);
            CloseHandle(hProcess);
        }
        char escaped[MAX_PATH * 2];
        JsonEscape(procName, escaped, sizeof(escaped));
        *len += snprintf(buf + *len, bufSize - *len,
            "%s{\"pid\":%lu,\"name\":\"%s\",\"cpu_pct\":%.1f,\"consecutive\":%d}",
            written > 0 ? "," : "",
            cs->pid, escaped, cs->cpuPercent, cs->consecutiveHigh);
        written++;
    }
    *len += snprintf(buf + *len, bufSize - *len, "]}");
}

static void WriteJsonGpu(char *buf, int bufSize, int *len) {
    *len = snprintf(buf, bufSize,
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
}

static void WriteJsonAnomalies(char *buf, int bufSize, int *len) {
    EnterCriticalSection(&g_csData);
    *len = snprintf(buf, bufSize, "{\"alerts\":[");
    int i;
    for (i = 0; i < g_anomalyCount; i++) {
        AnomalyAlert *a = &g_anomalies[i];
        const char *typeStr = "unknown";
        switch (a->type) {
        case ANOMALY_CPU_HOG:     typeStr = "cpu_hog";     break;
        case ANOMALY_MEM_LEAK:    typeStr = "mem_leak";    break;
        case ANOMALY_MEM_HOG:     typeStr = "mem_hog";     break;
        case ANOMALY_GPU_HOG:     typeStr = "gpu_hog";     break;
        case ANOMALY_SUSPICIOUS:  typeStr = "suspicious";  break;
        }
        char escDesc[512], escName[256];
        JsonEscape(a->description, escDesc, sizeof(escDesc));
        JsonEscape(a->procName, escName, sizeof(escName));
        *len += snprintf(buf + *len, bufSize - *len,
            "%s{\"t\":%I64d,\"type\":\"%s\",\"pid\":%lu,\"name\":\"%s\","
            "\"value\":%.1f,\"commit_mb\":%I64u,\"desc\":\"%s\"}",
            i > 0 ? "," : "",
            (long long)a->timestamp, typeStr, a->pid, escName,
            a->value, (unsigned long long)a->commitMB, escDesc);
    }
    *len += snprintf(buf + *len, bufSize - *len, "]}");
    LeaveCriticalSection(&g_csData);
}

/* New: aggregated history API for desktop charts */
static void WriteJsonAggregated(char *buf, int bufSize, int *len, int range) {
    EnterCriticalSection(&g_csData);

    AggBucket *buckets;
    int count;
    const char *labelFmt;

    switch (range) {
    case CHART_DAY:   /* hourly for last 24h */
        buckets = g_hourlyBuckets; count = g_hourlyCount; labelFmt = "hourly"; break;
    case CHART_WEEK:  /* daily for last 7d */
        buckets = g_dailyBuckets;  count = g_dailyCount;  labelFmt = "daily";  break;
    case CHART_MONTH: /* daily for last 30d */
        buckets = g_dailyBuckets;  count = g_dailyCount;  labelFmt = "daily";  break;
    case CHART_YEAR:  /* monthly */
        buckets = g_monthlyBuckets;count = g_monthlyCount; labelFmt = "monthly";break;
    default:
        buckets = g_dailyBuckets; count = g_dailyCount; labelFmt = "daily"; break;
    }

    int maxTake;
    switch (range) {
    case CHART_DAY:   maxTake = 24;  break;
    case CHART_WEEK:  maxTake = 7;   break;
    case CHART_MONTH: maxTake = 30;  break;
    case CHART_YEAR:  maxTake = 12;  break;
    default:          maxTake = 30;  break;
    }

    int take = count < maxTake ? count : maxTake;
    int start = count - take;
    if (start < 0) start = 0;

    *len = snprintf(buf, bufSize, "{\"range\":\"%s\",\"buckets\":[", labelFmt);
    int written = 0, i;
    for (i = start; i < count; i++) {
        AggBucket *b = &buckets[i];
        double pfAvg = b->sampleCount > 0 ? b->pfSum / b->sampleCount : 0;
        double phAvg = b->sampleCount > 0 ? b->phSum / b->sampleCount : 0;
        *len += snprintf(buf + *len, bufSize - *len,
            "%s{\"t\":%I64d,\"pf_avg\":%.1f,\"pf_max\":%.1f,"
            "\"ph_avg\":%.1f,\"ph_max\":%.1f,\"n\":%d}",
            written > 0 ? "," : "",
            (long long)b->bucketStart, pfAvg, b->pfMax,
            phAvg, b->phMax, b->sampleCount);
        written++;
    }
    *len += snprintf(buf + *len, bufSize - *len, "]}");
    LeaveCriticalSection(&g_csData);
}

/* New: suspicious processes API */
static void WriteJsonSuspicious(char *buf, int bufSize, int *len) {
    EnterCriticalSection(&g_csData);
    *len = snprintf(buf, bufSize, "{\"processes\":[");
    int written = 0, i;
    for (i = 0; i < g_suspProcCount; i++) {
        SuspiciousProc *sp = &g_suspProcs[i];
        char escaped[256];
        JsonEscape(sp->name, escaped, sizeof(escaped));
        SIZE_T growthMB = (sp->lastCommit > sp->firstCommit) ?
            ((sp->lastCommit - sp->firstCommit) / (1024 * 1024)) : 0;
        *len += snprintf(buf + *len, bufSize - *len,
            "%s{\"pid\":%lu,\"name\":\"%s\","
            "\"first_commit_mb\":%I64u,\"last_commit_mb\":%I64u,"
            "\"growth_mb\":%I64u,\"peak_rate\":%.1f,"
            "\"first_seen\":%I64d,\"last_seen\":%I64d,"
            "\"alerts\":%d}",
            written > 0 ? "," : "",
            sp->pid, escaped,
            (unsigned long long)(sp->firstCommit / (1024 * 1024)),
            (unsigned long long)(sp->lastCommit / (1024 * 1024)),
            (unsigned long long)growthMB, sp->peakGrowthRate,
            (long long)sp->firstSeen, (long long)sp->lastSeen,
            sp->alertCount);
        written++;
    }
    *len += snprintf(buf + *len, bufSize - *len, "]}");
    LeaveCriticalSection(&g_csData);
}

/* ============================================================================
 * HTTP response
 * ============================================================================ */
static void SendHttpResponse(SOCKET client, int status, const char *contentType,
                              const char *body, int bodyLen) {
    char header[512];
    int hdrLen = snprintf(header, sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s; charset=utf-8\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "\r\n",
        status, status == 200 ? "OK" : (status == 404 ? "Not Found" : "Error"),
        contentType, bodyLen);
    send(client, header, hdrLen, 0);
    if (body && bodyLen > 0)
        send(client, body, bodyLen, 0);
}

static void HandleRequest(SOCKET client, const char *method, const char *path) {
    if (strcmp(method, "GET") != 0) {
        SendHttpResponse(client, 404, "text/plain", "Not Found", 9);
        return;
    }

    /* Dashboard HTML — built in a separate include or inline */
    extern const char *const DASHBOARD_HTML;

    if (strcmp(path, "/") == 0 || strcmp(path, "/index.html") == 0) {
        SendHttpResponse(client, 200, "text/html",
                         DASHBOARD_HTML, (int)strlen(DASHBOARD_HTML));
        return;
    }

    if (strcmp(path, "/api/status") == 0) {
        static char buf[65536]; int len;
        WriteJsonStatus(buf, sizeof(buf), &len);
        SendHttpResponse(client, 200, "application/json", buf, len);
        return;
    }
    if (strcmp(path, "/api/history") == 0) {
        static char buf[131072]; int len;
        WriteJsonHistory(buf, sizeof(buf), &len, 24, 288);
        SendHttpResponse(client, 200, "application/json", buf, len);
        return;
    }
    if (strcmp(path, "/api/actions") == 0) {
        static char buf[65536]; int len;
        WriteJsonActions(buf, sizeof(buf), &len);
        SendHttpResponse(client, 200, "application/json", buf, len);
        return;
    }
    if (strcmp(path, "/api/cpu") == 0) {
        static char buf[32768]; int len;
        WriteJsonCpu(buf, sizeof(buf), &len);
        SendHttpResponse(client, 200, "application/json", buf, len);
        return;
    }
    if (strcmp(path, "/api/gpu") == 0) {
        static char buf[4096]; int len;
        WriteJsonGpu(buf, sizeof(buf), &len);
        SendHttpResponse(client, 200, "application/json", buf, len);
        return;
    }
    if (strcmp(path, "/api/anomalies") == 0) {
        static char buf[65536]; int len;
        WriteJsonAnomalies(buf, sizeof(buf), &len);
        SendHttpResponse(client, 200, "application/json", buf, len);
        return;
    }
    /* New endpoints for desktop charts */
    if (strncmp(path, "/api/aggregated", 16) == 0) {
        static char buf[32768]; int len;
        int range = CHART_WEEK; /* default */
        const char *qs = strchr(path, '?');
        if (qs) {
            if (strstr(qs, "range=day"))   range = CHART_DAY;
            if (strstr(qs, "range=week"))  range = CHART_WEEK;
            if (strstr(qs, "range=month")) range = CHART_MONTH;
            if (strstr(qs, "range=year"))  range = CHART_YEAR;
        }
        WriteJsonAggregated(buf, sizeof(buf), &len, range);
        SendHttpResponse(client, 200, "application/json", buf, len);
        return;
    }
    if (strcmp(path, "/api/suspicious") == 0) {
        static char buf[32768]; int len;
        WriteJsonSuspicious(buf, sizeof(buf), &len);
        SendHttpResponse(client, 200, "application/json", buf, len);
        return;
    }

    SendHttpResponse(client, 404, "text/plain", "Not Found", 9);
}

/* ============================================================================
 * HTTP server thread
 * ============================================================================ */
DWORD WINAPI HttpServerThread(LPVOID lpParam) {
    (void)lpParam;
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        Log("WSAStartup failed");
        return 1;
    }

    SOCKET listenSocket = INVALID_SOCKET;
    int port;
    for (port = HTTP_PORT_START; port <= HTTP_PORT_END; port++) {
        listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (listenSocket == INVALID_SOCKET) continue;

        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = inet_addr("127.0.0.1");
        addr.sin_port = htons((u_short)port);

        if (bind(listenSocket, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
            g_httpPort = port;
            break;
        }
        closesocket(listenSocket);
        listenSocket = INVALID_SOCKET;
    }

    if (listenSocket == INVALID_SOCKET) {
        Log("HTTP server failed: ports %d-%d all occupied",
            HTTP_PORT_START, HTTP_PORT_END);
        WSACleanup();
        return 1;
    }

    if (listen(listenSocket, SOMAXCONN) == SOCKET_ERROR) {
        Log("listen() failed");
        closesocket(listenSocket);
        WSACleanup();
        return 1;
    }

    Log("Dashboard started: http://127.0.0.1:%d", g_httpPort);

    DWORD timeout = 1000;
    while (g_bRunning) {
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(listenSocket, &readSet);

        struct timeval tv = { 1, 0 };
        int selRet = select(0, &readSet, NULL, NULL, &tv);
        if (selRet <= 0) continue;

        SOCKET client = accept(listenSocket, NULL, NULL);
        if (client == INVALID_SOCKET) continue;

        setsockopt(client, SOL_SOCKET, SO_RCVTIMEO,
                   (const char *)&timeout, sizeof(timeout));

        char request[8192];
        int recvLen = recv(client, request, sizeof(request) - 1, 0);
        if (recvLen > 0) {
            request[recvLen] = '\0';

            char method[16] = {0}, path[256] = {0};
            sscanf(request, "%15s %255s", method, path);

            /* URL decode */
            char decoded[256]; int di = 0, si;
            for (si = 0; path[si] && di < 254; si++) {
                if (path[si] == '%' && path[si+1] && path[si+2]) {
                    char hex[3] = { path[si+1], path[si+2], 0 };
                    decoded[di++] = (char)strtol(hex, NULL, 16);
                    si += 2;
                } else {
                    decoded[di++] = path[si];
                }
            }
            decoded[di] = '\0';
            HandleRequest(client, method, decoded);
        }
        closesocket(client);
    }

    closesocket(listenSocket);
    WSACleanup();
    Log("HTTP server stopped");
    return 0;
}
