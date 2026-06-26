/**
 * vm_engine.c — Core engine: logging, snapshot collection, CPU/GPU sampling,
 *                anomaly detection, memory cleanup, suspicious process tracking.
 */
#include "vm_common.h"

/* ============================================================================
 * Global state definitions
 * ============================================================================ */
HANDLE  g_hLogFile        = INVALID_HANDLE_VALUE;
BOOL    g_bRunning         = TRUE;
BOOL    g_bConsole         = FALSE;
BOOL    g_bDesktop         = FALSE;
time_t  g_tLastAction      = 0;
time_t  g_tStartTime       = 0;
int     g_httpPort         = 0;

CRITICAL_SECTION g_csData;
MemorySnapshot  g_snapshots[MAX_HISTORY_SNAPSHOTS];
int             g_snapshotCount = 0;
MemorySnapshot  g_latestSnapshot;
ActionRecord    g_actions[MAX_ACTION_LOG];
int             g_actionCount = 0;

AggBucket  g_hourlyBuckets[MAX_HOURLY_POINTS];
int        g_hourlyCount = 0;
AggBucket  g_dailyBuckets[MAX_DAILY_POINTS];
int        g_dailyCount = 0;
AggBucket  g_monthlyBuckets[MAX_MONTHLY_POINTS];
int        g_monthlyCount = 0;

HANDLE  g_hDbFile = INVALID_HANDLE_VALUE;

CpuSample     g_cpuSamples[MAX_CPU_SAMPLE_PIDS];
int           g_cpuSampleCount = 0;
GpuInfo       g_gpuInfo;
AnomalyAlert  g_anomalies[MAX_ANOMALY_ALERTS];
int           g_anomalyCount = 0;
DWORD         g_numProcessors = 1;
ULONGLONG     g_lastCpuSampleTick = 0;

SuspiciousProc g_suspProcs[MAX_SUSPICIOUS_PROCS];
int            g_suspProcCount = 0;

PFN_PdhOpenQueryA                g_pfnPdhOpenQueryA = NULL;
PFN_PdhAddCounterA               g_pfnPdhAddCounterA = NULL;
PFN_PdhAddEnglishCounterA        g_pfnPdhAddEnglishCounterA = NULL;
PFN_PdhCollectQueryData          g_pfnPdhCollectQueryData = NULL;
PFN_PdhGetFormattedCounterArrayA  g_pfnPdhGetFormattedCounterArrayA = NULL;
PFN_PdhCloseQuery                g_pfnPdhCloseQuery = NULL;
HMODULE    g_hPdhDll          = NULL;
PDH_HQUERY    g_hPdhQuery     = NULL;
PDH_HCOUNTER  g_hPdhCounterGpu = NULL;
BOOL          g_bPdhAvailable = FALSE;

HANDLE  g_hHttpThread = NULL;

__declspec(dllimport) ULONGLONG WINAPI GetTickCount64(void);

/* ============================================================================
 * Logging
 * ============================================================================ */
void Log(const char *format, ...) {
    char buf[1024];
    va_list args;
    va_start(args, format);
    int len = vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);

    SYSTEMTIME st;
    GetLocalTime(&st);
    char timestamp[64];
    snprintf(timestamp, sizeof(timestamp),
             "[%04d-%02d-%02d %02d:%02d:%02d] ",
             st.wYear, st.wMonth, st.wDay,
             st.wHour, st.wMinute, st.wSecond);

    if (g_bConsole) {
        printf("%s%s\n", timestamp, buf);
        fflush(stdout);
    }
    if (g_hLogFile != INVALID_HANDLE_VALUE) {
        DWORD written;
        WriteFile(g_hLogFile, timestamp, (DWORD)strlen(timestamp), &written, NULL);
        WriteFile(g_hLogFile, buf, (DWORD)len, &written, NULL);
        WriteFile(g_hLogFile, "\r\n", 2, &written, NULL);
        FlushFileBuffers(g_hLogFile);
    }
}

BOOL InitLogFile(void) {
    char path[MAX_PATH];
    GetModuleFileNameA(NULL, path, sizeof(path));
    char *lastSlash = strrchr(path, '\\');
    if (lastSlash) *(lastSlash + 1) = '\0';
    strncat(path, LOG_FILE_NAME, sizeof(path) - strlen(path) - 1);

    g_hLogFile = CreateFileA(path, FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, NULL);
    return (g_hLogFile != INVALID_HANDLE_VALUE);
}

/* ============================================================================
 * Privilege escalation
 * ============================================================================ */
BOOL EnableDebugPrivilege(void) {
    HANDLE hToken;
    if (!OpenProcessToken(GetCurrentProcess(),
                          TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken))
        return FALSE;
    TOKEN_PRIVILEGES tp;
    LUID luid;
    if (!LookupPrivilegeValueA(NULL, SE_DEBUG_NAME, &luid)) {
        CloseHandle(hToken); return FALSE;
    }
    tp.PrivilegeCount = 1;
    tp.Privileges[0].Luid = luid;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    BOOL result = AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(tp), NULL, NULL);
    CloseHandle(hToken);
    return result && GetLastError() == ERROR_SUCCESS;
}

/* ============================================================================
 * Memory monitoring
 * ============================================================================ */
DWORD GetIdleTimeMs(void) {
    LASTINPUTINFO lii = { sizeof(LASTINPUTINFO) };
    if (!GetLastInputInfo(&lii)) return 0;
    return GetTickCount() - lii.dwTime;
}

DWORD GetPageFileUsagePct(void) {
    MEMORYSTATUSEX msx = { sizeof(MEMORYSTATUSEX) };
    if (!GlobalMemoryStatusEx(&msx)) return 0;
    if (msx.ullTotalPageFile == 0) return 0;
    ULONGLONG used = msx.ullTotalPageFile - msx.ullAvailPageFile;
    return (DWORD)(used * 100 / msx.ullTotalPageFile);
}

static SIZE_T GetProcessMemoryInfoEx(DWORD pid, SIZE_T *commit, SIZE_T *ws) {
    HANDLE hProcess = OpenProcess(
        PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!hProcess) return 0;

    PROCESS_MEMORY_COUNTERS_EX pmc;
    memset(&pmc, 0, sizeof(pmc));
    pmc.cb = sizeof(pmc);
    if (GetProcessMemoryInfo(hProcess, (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc))) {
        *commit = pmc.PrivateUsage;
        *ws = pmc.WorkingSetSize;
    }
    CloseHandle(hProcess);
    return *commit;
}

static int CompareProcInfo(const void *a, const void *b) {
    const ProcessInfo *pa = (const ProcessInfo *)a;
    const ProcessInfo *pb = (const ProcessInfo *)b;
    if (pa->commitSize > pb->commitSize) return -1;
    if (pa->commitSize < pb->commitSize) return 1;
    return 0;
}

void CollectSnapshot(MemorySnapshot *snap) {
    memset(snap, 0, sizeof(*snap));
    snap->timestamp = time(NULL);

    MEMORYSTATUSEX msx = { sizeof(MEMORYSTATUSEX) };
    if (GlobalMemoryStatusEx(&msx)) {
        snap->totalPhys     = msx.ullTotalPhys;
        snap->availPhys     = msx.ullAvailPhys;
        snap->totalPageFile = msx.ullTotalPageFile;
        snap->availPageFile = msx.ullAvailPageFile;
        snap->physLoad      = msx.dwMemoryLoad;
        if (msx.ullTotalPageFile > 0) {
            ULONGLONG used = msx.ullTotalPageFile - msx.ullAvailPageFile;
            snap->pageFilePct = (DWORD)(used * 100 / msx.ullTotalPageFile);
        }
    }
    snap->idleSeconds = GetIdleTimeMs() / 1000;

    /* ---- Process enumeration ---- */
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot != INVALID_HANDLE_VALUE) {
        ProcessInfo *procs = NULL;
        DWORD count = 0, capacity = 512;
        procs = (ProcessInfo *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                                         capacity * sizeof(ProcessInfo));
        if (procs) {
            PROCESSENTRY32 pe;
            memset(&pe, 0, sizeof(pe));
            pe.dwSize = sizeof(PROCESSENTRY32);
            if (Process32First(hSnapshot, &pe)) {
                do {
                    SIZE_T commit = 0, ws = 0;
                    GetProcessMemoryInfoEx(pe.th32ProcessID, &commit, &ws);
                    if (commit > 0) {
                        if (count >= capacity) {
                            capacity *= 2;
                            ProcessInfo *tmp = (ProcessInfo *)HeapReAlloc(
                                GetProcessHeap(), HEAP_ZERO_MEMORY, procs,
                                capacity * sizeof(ProcessInfo));
                            if (!tmp) break;
                            procs = tmp;
                        }
                        procs[count].pid = pe.th32ProcessID;
                        procs[count].commitSize = commit;
                        procs[count].workingSet = ws;
                        strncpy(procs[count].name, pe.szExeFile,
                                sizeof(procs[count].name) - 1);
                        count++;
                    }
                } while (Process32Next(hSnapshot, &pe));
            }
            qsort(procs, count, sizeof(ProcessInfo), CompareProcInfo);
            int n = (int)(count < MAX_TOP_PROCESSES ? count : MAX_TOP_PROCESSES);
            snap->numProcesses = n;
            int i;
            for (i = 0; i < n; i++) {
                ProcessInfo *newP = &snap->topProcesses[i];
                memcpy(newP, &procs[i], sizeof(ProcessInfo));

                /* Match with previous snapshot for growth tracking */
                EnterCriticalSection(&g_csData);
                int j;
                for (j = 0; j < g_latestSnapshot.numProcesses; j++) {
                    ProcessInfo *oldP = &g_latestSnapshot.topProcesses[j];
                    if (oldP->pid == newP->pid) {
                        newP->prevCommit = oldP->commitSize;
                        newP->prevSampleTime = g_latestSnapshot.timestamp;
                        break;
                    }
                }
                LeaveCriticalSection(&g_csData);

                /* Calculate growth rate */
                if (newP->prevCommit > 0 && newP->prevSampleTime > 0) {
                    time_t deltaT = snap->timestamp - newP->prevSampleTime;
                    if (deltaT > 0 && newP->commitSize > newP->prevCommit) {
                        SIZE_T deltaCommit = newP->commitSize - newP->prevCommit;
                        newP->growthRateMBps = (double)deltaCommit /
                            (1024.0 * 1024.0) / (double)deltaT;
                    }
                }
            }
            HeapFree(GetProcessHeap(), 0, procs);
        }
        CloseHandle(hSnapshot);
    }
}

/* ============================================================================
 * Aggregated history — hourly / daily / monthly buckets for charts
 * ============================================================================ */
void UpdateAggregations(MemorySnapshot *snap) {
    EnterCriticalSection(&g_csData);

    /* Hourly aggregation */
    if (g_hourlyCount > 0) {
        AggBucket *hb = &g_hourlyBuckets[g_hourlyCount - 1];
        if (snap->timestamp < hb->bucketStart + 3600) {
            hb->pfSum += snap->pageFilePct;
            hb->phSum += snap->physLoad;
            if (snap->pageFilePct > hb->pfMax) hb->pfMax = snap->pageFilePct;
            if (snap->physLoad > hb->phMax) hb->phMax = snap->physLoad;
            hb->sampleCount++;
            goto daily_check;
        }
    }
    {
        if (g_hourlyCount >= MAX_HOURLY_POINTS) {
            memmove(g_hourlyBuckets, g_hourlyBuckets + 1,
                    (MAX_HOURLY_POINTS - 1) * sizeof(AggBucket));
            g_hourlyCount = MAX_HOURLY_POINTS - 1;
        }
        AggBucket *hb = &g_hourlyBuckets[g_hourlyCount++];
        memset(hb, 0, sizeof(*hb));
        hb->bucketStart = snap->timestamp;
        hb->pfSum = hb->pfMax = snap->pageFilePct;
        hb->phSum = hb->phMax = snap->physLoad;
        hb->sampleCount = 1;
    }

daily_check:
    /* Daily aggregation */
    {
        time_t todayStart = snap->timestamp - (snap->timestamp % 86400);
        if (g_dailyCount > 0) {
            AggBucket *db = &g_dailyBuckets[g_dailyCount - 1];
            if (todayStart <= db->bucketStart) {
                db->pfSum += snap->pageFilePct;
                db->phSum += snap->physLoad;
                if (snap->pageFilePct > db->pfMax) db->pfMax = snap->pageFilePct;
                if (snap->physLoad > db->phMax) db->phMax = snap->physLoad;
                db->sampleCount++;
                goto monthly_check;
            }
        }
        if (g_dailyCount >= MAX_DAILY_POINTS) {
            memmove(g_dailyBuckets, g_dailyBuckets + 1,
                    (MAX_DAILY_POINTS - 1) * sizeof(AggBucket));
            g_dailyCount = MAX_DAILY_POINTS - 1;
        }
        AggBucket *db = &g_dailyBuckets[g_dailyCount++];
        memset(db, 0, sizeof(*db));
        db->bucketStart = todayStart;
        db->pfSum = db->pfMax = snap->pageFilePct;
        db->phSum = db->phMax = snap->physLoad;
        db->sampleCount = 1;
    }

monthly_check:
    /* Monthly aggregation */
    {
        time_t now = snap->timestamp;
        struct tm *tmNow = gmtime(&now);
        time_t monthStart = now - (tmNow->tm_mday - 1) * 86400
                            - tmNow->tm_hour * 3600
                            - tmNow->tm_min * 60 - tmNow->tm_sec;
        if (g_monthlyCount > 0) {
            AggBucket *mb = &g_monthlyBuckets[g_monthlyCount - 1];
            if (monthStart <= mb->bucketStart) {
                mb->pfSum += snap->pageFilePct;
                mb->phSum += snap->physLoad;
                if (snap->pageFilePct > mb->pfMax) mb->pfMax = snap->pageFilePct;
                if (snap->physLoad > mb->phMax) mb->phMax = snap->physLoad;
                mb->sampleCount++;
            } else {
                if (g_monthlyCount >= MAX_MONTHLY_POINTS) {
                    memmove(g_monthlyBuckets, g_monthlyBuckets + 1,
                            (MAX_MONTHLY_POINTS - 1) * sizeof(AggBucket));
                    g_monthlyCount = MAX_MONTHLY_POINTS - 1;
                }
                AggBucket *mb2 = &g_monthlyBuckets[g_monthlyCount++];
                memset(mb2, 0, sizeof(*mb2));
                mb2->bucketStart = monthStart;
                mb2->pfSum = mb2->pfMax = snap->pageFilePct;
                mb2->phSum = mb2->phMax = snap->physLoad;
                mb2->sampleCount = 1;
            }
        } else {
            AggBucket *mb = &g_monthlyBuckets[g_monthlyCount++];
            memset(mb, 0, sizeof(*mb));
            mb->bucketStart = monthStart;
            mb->pfSum = mb->pfMax = snap->pageFilePct;
            mb->phSum = mb->phMax = snap->physLoad;
            mb->sampleCount = 1;
        }
    }

    LeaveCriticalSection(&g_csData);
}

/* ============================================================================
 * Suspicious process tracking — detect rapid memory growth
 * ============================================================================ */
void TrackSuspiciousProcesses(void) {
    EnterCriticalSection(&g_csData);
    int i;
    for (i = 0; i < g_latestSnapshot.numProcesses; i++) {
        ProcessInfo *proc = &g_latestSnapshot.topProcesses[i];
        SIZE_T commitMB = proc->commitSize / (1024 * 1024);

        if (commitMB >= SUSPICIOUS_COMMIT_MIN_MB &&
            proc->growthRateMBps > SUSPICIOUS_GROWTH_MB_PER_SEC) {

            /* Check if already tracked */
            BOOL found = FALSE;
            int j;
            for (j = 0; j < g_suspProcCount; j++) {
                if (g_suspProcs[j].pid == proc->pid) {
                    SuspiciousProc *sp = &g_suspProcs[j];
                    sp->lastCommit = proc->commitSize;
                    sp->lastSeen = time(NULL);
                    if (proc->growthRateMBps > sp->peakGrowthRate)
                        sp->peakGrowthRate = proc->growthRateMBps;
                    sp->alertCount++;
                    found = TRUE;

                    /* Generate anomaly alert (debounce: 5 min) */
                    if (sp->alertCount >= 3 && g_anomalyCount < MAX_ANOMALY_ALERTS) {
                        BOOL alreadyAlerted = FALSE;
                        int k;
                        for (k = 0; k < g_anomalyCount; k++) {
                            if (g_anomalies[k].pid == proc->pid &&
                                g_anomalies[k].type == ANOMALY_SUSPICIOUS &&
                                time(NULL) - g_anomalies[k].timestamp < 300) {
                                alreadyAlerted = TRUE; break;
                            }
                        }
                        if (!alreadyAlerted) {
                            AnomalyAlert *a = &g_anomalies[g_anomalyCount++];
                            memset(a, 0, sizeof(*a));
                            a->timestamp = time(NULL);
                            a->type = ANOMALY_SUSPICIOUS;
                            a->pid = proc->pid;
                            strncpy(a->procName, proc->name, sizeof(a->procName) - 1);
                            a->value = proc->growthRateMBps;
                            a->commitMB = commitMB;
                            snprintf(a->description, sizeof(a->description),
                                "Suspicious: %s (PID=%lu) mem growth %.1f MB/s, Commit %I64u MB",
                                proc->name, proc->pid, proc->growthRateMBps,
                                (unsigned long long)commitMB);
                            Log("[SUSPICIOUS] %s", a->description);
                        }
                    }
                    break;
                }
            }

            if (!found && g_suspProcCount < MAX_SUSPICIOUS_PROCS) {
                SuspiciousProc *sp = &g_suspProcs[g_suspProcCount++];
                memset(sp, 0, sizeof(*sp));
                sp->pid = proc->pid;
                strncpy(sp->name, proc->name, sizeof(sp->name) - 1);
                sp->firstCommit = proc->commitSize;
                sp->lastCommit = proc->commitSize;
                sp->firstSeen = time(NULL);
                sp->lastSeen = time(NULL);
                sp->peakGrowthRate = proc->growthRateMBps;
                sp->alertCount = 1;
            }
        }
    }
    LeaveCriticalSection(&g_csData);
}

/* ============================================================================
 * CPU usage sampling — two-pass differential via GetProcessTimes
 * ============================================================================ */
void SampleProcessCpu(void) {
    ULONGLONG nowTick = GetTickCount64();
    ULONGLONG tickDelta = nowTick - g_lastCpuSampleTick;
    g_lastCpuSampleTick = nowTick;

    BOOL isFirstSample = (g_cpuSampleCount == 0);

    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE) return;

    PROCESSENTRY32 pe;
    memset(&pe, 0, sizeof(pe));
    pe.dwSize = sizeof(PROCESSENTRY32);

    if (Process32First(hSnapshot, &pe)) {
        do {
            DWORD pid = pe.th32ProcessID;
            if (pid == 0 || pid == 4) continue;  /* skip Idle & System */

            HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pid);
            if (!hProcess) continue;

            FILETIME ftCreate, ftExit, ftKernel, ftUser;
            if (GetProcessTimes(hProcess, &ftCreate, &ftExit, &ftKernel, &ftUser)) {
                ULONGLONG kernelTime = ((ULONGLONG)ftKernel.dwHighDateTime << 32) | ftKernel.dwLowDateTime;
                ULONGLONG userTime   = ((ULONGLONG)ftUser.dwHighDateTime << 32)   | ftUser.dwLowDateTime;

                /* Find existing sample */
                int found = -1, s;
                for (s = 0; s < g_cpuSampleCount; s++) {
                    if (g_cpuSamples[s].pid == pid) { found = s; break; }
                }

                if (found >= 0 && !isFirstSample && tickDelta > 0) {
                    CpuSample *cs = &g_cpuSamples[found];
                    ULONGLONG deltaKernel = kernelTime - cs->prevKernelTime;
                    ULONGLONG deltaUser   = userTime   - cs->prevUserTime;
                    ULONGLONG deltaTotal  = deltaKernel + deltaUser;

                    double cpuPct = (double)deltaTotal /
                        (double)(tickDelta * 10000ULL * g_numProcessors) * 100.0;
                    if (cpuPct < 0) cpuPct = 0;
                    if (cpuPct > 100) cpuPct = 100;
                    cs->cpuPercent = cpuPct;
                } else if (found < 0 && g_cpuSampleCount < MAX_CPU_SAMPLE_PIDS) {
                    CpuSample *cs = &g_cpuSamples[g_cpuSampleCount++];
                    memset(cs, 0, sizeof(*cs));
                    cs->pid = pid;
                }

                /* Update baselines */
                {
                    int idx = -1, s2;
                    for (s2 = 0; s2 < g_cpuSampleCount; s2++) {
                        if (g_cpuSamples[s2].pid == pid) { idx = s2; break; }
                    }
                    if (idx >= 0) {
                        g_cpuSamples[idx].prevKernelTime = kernelTime;
                        g_cpuSamples[idx].prevUserTime   = userTime;
                        g_cpuSamples[idx].prevSampleTick  = nowTick;
                    }
                }
            }
            CloseHandle(hProcess);
        } while (Process32Next(hSnapshot, &pe));
    }
    CloseHandle(hSnapshot);

    /* Purge exited processes */
    int writePos = 0, si;
    for (si = 0; si < g_cpuSampleCount; si++) {
        HANDLE hTest = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,
                                   FALSE, g_cpuSamples[si].pid);
        if (hTest) {
            CloseHandle(hTest);
            if (writePos != si)
                memcpy(&g_cpuSamples[writePos], &g_cpuSamples[si], sizeof(CpuSample));
            writePos++;
        }
    }
    g_cpuSampleCount = writePos;
}

/* ============================================================================
 * GPU monitoring — dynamic PDH loading
 * ============================================================================ */
static BOOL LoadPdhLibrary(void) {
    if (g_hPdhDll) return TRUE;
    g_hPdhDll = LoadLibraryA("pdh.dll");
    if (!g_hPdhDll) return FALSE;
    g_pfnPdhOpenQueryA    = (PFN_PdhOpenQueryA)    GetProcAddress(g_hPdhDll, "PdhOpenQueryA");
    g_pfnPdhAddCounterA   = (PFN_PdhAddCounterA)   GetProcAddress(g_hPdhDll, "PdhAddCounterA");
    /* PdhAddEnglishCounterA available on Vista+ — use English counter names
       regardless of system locale (critical for zh-CN/zh-TW Windows) */
    g_pfnPdhAddEnglishCounterA = (PFN_PdhAddEnglishCounterA) GetProcAddress(g_hPdhDll, "PdhAddEnglishCounterA");
    g_pfnPdhCollectQueryData         = (PFN_PdhCollectQueryData)         GetProcAddress(g_hPdhDll, "PdhCollectQueryData");
    g_pfnPdhGetFormattedCounterArrayA = (PFN_PdhGetFormattedCounterArrayA)GetProcAddress(g_hPdhDll, "PdhGetFormattedCounterArrayA");
    g_pfnPdhCloseQuery               = (PFN_PdhCloseQuery)               GetProcAddress(g_hPdhDll, "PdhCloseQuery");
    return (g_pfnPdhOpenQueryA && g_pfnPdhCollectQueryData);
}

void InitGpuMonitoring(void) {
    memset(&g_gpuInfo, 0, sizeof(g_gpuInfo));
    g_gpuInfo.utilization = -1;

    if (!LoadPdhLibrary()) {
        Log("GPU: pdh.dll not available");
        return;
    }

    /* ---- Open PDH query ---- */
    PDH_STATUS status = g_pfnPdhOpenQueryA(NULL, 0, &g_hPdhQuery);
    if (status != ERROR_SUCCESS) {
        Log("GPU: PdhOpenQueryA failed (0x%08lX)", (unsigned long)status);
        return;
    }

    /* ---- Add GPU utilization counter ----
       Try multiple paths because different GPU drivers expose
       different counter names.  PdhAddEnglishCounterA uses the
       locale-independent English name (critical on Chinese Windows). */
    static const char *gpuCounterPaths[] = {
        "\\GPU Engine(*engtype_3d)\\Utilization Percentage",
        "\\GPU Engine(*engtype_3D)\\Utilization Percentage",
        "\\GPU Engine(*)\\Utilization Percentage",
        "\\GPU Adapter(*)\\Utilization Percentage",
    };

    BOOL counterAdded = FALSE;
    int ci;
    for (ci = 0; ci < 4; ci++) {
        /* Try English counter first (works on all locales) */
        if (g_pfnPdhAddEnglishCounterA) {
            status = g_pfnPdhAddEnglishCounterA(g_hPdhQuery,
                gpuCounterPaths[ci], 0, &g_hPdhCounterGpu);
            if (status == ERROR_SUCCESS) { counterAdded = TRUE; break; }
        }
        /* Fallback to localized counter */
        status = g_pfnPdhAddCounterA(g_hPdhQuery,
            gpuCounterPaths[ci], 0, &g_hPdhCounterGpu);
        if (status == ERROR_SUCCESS) { counterAdded = TRUE; break; }
    }

    if (!counterAdded) {
        Log("GPU: no utilization counter available (0x%08lX)", (unsigned long)status);
        g_pfnPdhCloseQuery(g_hPdhQuery);
        g_hPdhQuery = NULL;
        return;
    }

    /* Prime the first data collection */
    status = g_pfnPdhCollectQueryData(g_hPdhQuery);
    if (status == ERROR_SUCCESS) {
        g_bPdhAvailable = TRUE;
        Log("GPU: PDH counter initialized, path=%s", gpuCounterPaths[ci]);
    } else {
        Log("GPU: initial CollectQueryData failed (0x%08lX)", (unsigned long)status);
    }

    /* ---- Query GPU name and VRAM from registry ----
       Enumerate subkeys 0000-0009 to find the active GPU
       (laptops often have iGPU at 0000 and dGPU at 0001/0002) */
    {
        const char *classGuid = "SYSTEM\\CurrentControlSet\\Control\\"
                                "Class\\{4d36e968-e325-11ce-bfc1-08002be10318}";
        char subkey[256];
        int ki;
        for (ki = 0; ki <= 9; ki++) {
            snprintf(subkey, sizeof(subkey), "%s\\%04d", classGuid, ki);
            HKEY hKey;
            if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, subkey, 0, KEY_READ, &hKey) != ERROR_SUCCESS)
                continue;

            /* Check if this is a GPU adapter (has DriverDesc) */
            DWORD size = sizeof(g_gpuInfo.name);
            memset(g_gpuInfo.name, 0, sizeof(g_gpuInfo.name));
            if (RegQueryValueExA(hKey, "DriverDesc", NULL, NULL,
                    (BYTE*)g_gpuInfo.name, &size) != ERROR_SUCCESS || g_gpuInfo.name[0] == 0) {
                RegCloseKey(hKey);
                continue;
            }

            /* Prefer the adapter with actual VRAM (skip Microsoft Basic Display) */
            DWORD vramBytes = 0;
            size = sizeof(vramBytes);
            if (RegQueryValueExA(hKey, "HardwareInformation.qwMemorySize",
                    NULL, NULL, (BYTE*)&vramBytes, &size) == ERROR_SUCCESS && vramBytes > 0) {
                g_gpuInfo.dedicatedTotal = (SIZE_T)vramBytes;
                g_gpuInfo.available = TRUE;
                Log("GPU: %s, VRAM=%I64u MB (key=%s)",
                    g_gpuInfo.name,
                    (unsigned long long)(vramBytes / (1024 * 1024)),
                    subkey);
                RegCloseKey(hKey);
                return;  /* Found a real GPU — done */
            }

            /* Check alternate VRAM key */
            size = sizeof(vramBytes);
            if (RegQueryValueExA(hKey, "HardwareInformation.MemorySize",
                    NULL, NULL, (BYTE*)&vramBytes, &size) == ERROR_SUCCESS && vramBytes > 0) {
                g_gpuInfo.dedicatedTotal = (SIZE_T)vramBytes;
                g_gpuInfo.available = TRUE;
                Log("GPU: %s, VRAM=%I64u MB (alt key, %s)",
                    g_gpuInfo.name,
                    (unsigned long long)(vramBytes / (1024 * 1024)),
                    subkey);
                RegCloseKey(hKey);
                return;  /* Found a real GPU — done */
            }

            /* May be a software adapter — keep looking */
            RegCloseKey(hKey);
        }

        /* Fallback: use the first adapter that has a name, even without VRAM */
        if (!g_gpuInfo.available) {
            for (ki = 0; ki <= 9; ki++) {
                snprintf(subkey, sizeof(subkey), "%s\\%04d", classGuid, ki);
                HKEY hKey;
                if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, subkey, 0, KEY_READ, &hKey) != ERROR_SUCCESS)
                    continue;
                DWORD size = sizeof(g_gpuInfo.name);
                if (RegQueryValueExA(hKey, "DriverDesc", NULL, NULL,
                        (BYTE*)g_gpuInfo.name, &size) == ERROR_SUCCESS && g_gpuInfo.name[0] != 0) {
                    /* Skip known software-only adapters */
                    if (strstr(g_gpuInfo.name, "Microsoft Basic") ||
                        strstr(g_gpuInfo.name, "Microsoft Hyper-V")) {
                        RegCloseKey(hKey);
                        continue;
                    }
                    g_gpuInfo.available = TRUE;
                    Log("GPU: %s (VRAM unknown, key=%s)", g_gpuInfo.name, subkey);
                    RegCloseKey(hKey);
                    return;
                }
                RegCloseKey(hKey);
            }
        }
    }

    if (!g_gpuInfo.available)
        Log("GPU: no adapter found in registry (keys 0000-0009)");
}

void QueryGpuInfo(void) {
    if (!g_bPdhAvailable || !g_hPdhQuery || !g_pfnPdhCollectQueryData) return;
    PDH_STATUS status = g_pfnPdhCollectQueryData(g_hPdhQuery);
    if (status != ERROR_SUCCESS) return;

    PDH_FMT_COUNTERVALUE_ITEM_A *pItems = NULL;
    DWORD dwItemCount = 0, dwBufSize = 0;
    status = g_pfnPdhGetFormattedCounterArrayA(g_hPdhCounterGpu,
        PDH_FMT_DOUBLE, &dwBufSize, &dwItemCount, NULL);
    if (status != PDH_MORE_DATA) return;

    pItems = (PDH_FMT_COUNTERVALUE_ITEM_A*)HeapAlloc(GetProcessHeap(), 0, dwBufSize);
    if (!pItems) return;

    status = g_pfnPdhGetFormattedCounterArrayA(g_hPdhCounterGpu,
        PDH_FMT_DOUBLE, &dwBufSize, &dwItemCount, pItems);
    if (status == ERROR_SUCCESS && dwItemCount > 0) {
        double totalUtil = 0;
        DWORD i;
        for (i = 0; i < dwItemCount; i++) {
            if (pItems[i].FmtValue.CStatus == ERROR_SUCCESS)
                totalUtil += pItems[i].FmtValue.doubleValue;
        }
        if (totalUtil > 100) totalUtil = 100;
        g_gpuInfo.utilization = (int)totalUtil;
    }
    HeapFree(GetProcessHeap(), 0, pItems);
}

void ShutdownGpuMonitoring(void) {
    if (g_hPdhQuery && g_pfnPdhCloseQuery) { g_pfnPdhCloseQuery(g_hPdhQuery); g_hPdhQuery = NULL; }
    if (g_hPdhDll) { FreeLibrary(g_hPdhDll); g_hPdhDll = NULL; }
}

/* ============================================================================
 * Anomaly detection
 * ============================================================================ */
void DetectAnomalies(void) {
    int i;

    /* CPU anomalies */
    for (i = 0; i < g_cpuSampleCount; i++) {
        CpuSample *cs = &g_cpuSamples[i];
        if (cs->cpuPercent > CPU_HOG_THRESHOLD_PCT)
            cs->consecutiveHigh++;
        else
            cs->consecutiveHigh = 0;

        if (cs->consecutiveHigh >= CPU_ANOMALY_SAMPLES && g_anomalyCount < MAX_ANOMALY_ALERTS) {
            char procName[MAX_PATH] = "unknown";
            HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ,
                                          FALSE, cs->pid);
            if (hProcess) {
                HMODULE hMod; DWORD needed;
                if (EnumProcessModules(hProcess, &hMod, sizeof(hMod), &needed))
                    GetModuleBaseNameA(hProcess, hMod, procName, sizeof(procName) - 1);
                CloseHandle(hProcess);
            }
            AnomalyAlert *a = &g_anomalies[g_anomalyCount++];
            memset(a, 0, sizeof(*a));
            a->timestamp = time(NULL);
            a->type = ANOMALY_CPU_HOG;
            a->pid = cs->pid;
            strncpy(a->procName, procName, sizeof(a->procName) - 1);
            a->value = cs->cpuPercent;
            snprintf(a->description, sizeof(a->description),
                "Process %s (PID=%lu) CPU %.1f%%, consecutive %d times",
                procName, cs->pid, cs->cpuPercent, CPU_ANOMALY_SAMPLES);
            Log("[CPU ANOMALY] %s", a->description);
            cs->consecutiveHigh = 0;
        }
    }

    /* Memory anomalies */
    EnterCriticalSection(&g_csData);
    int j;
    for (j = 0; j < g_latestSnapshot.numProcesses && g_anomalyCount < MAX_ANOMALY_ALERTS; j++) {
        ProcessInfo *proc = &g_latestSnapshot.topProcesses[j];
        SIZE_T commitMB = proc->commitSize / (1024 * 1024);
        if (commitMB > MEM_HOG_THRESHOLD_MB) {
            BOOL dup = FALSE;
            int k;
            for (k = 0; k < g_anomalyCount; k++) {
                if (g_anomalies[k].pid == proc->pid &&
                    g_anomalies[k].type == ANOMALY_MEM_HOG &&
                    time(NULL) - g_anomalies[k].timestamp < 3600) {
                    dup = TRUE; break;
                }
            }
            if (dup) continue;
            AnomalyAlert *a = &g_anomalies[g_anomalyCount++];
            memset(a, 0, sizeof(*a));
            a->timestamp = time(NULL);
            a->type = ANOMALY_MEM_HOG;
            a->pid = proc->pid;
            strncpy(a->procName, proc->name, sizeof(a->procName) - 1);
            a->value = (double)commitMB;
            a->commitMB = commitMB;
            snprintf(a->description, sizeof(a->description),
                "Process %s (PID=%lu) commit %I64u MB, exceeds %d MB limit",
                proc->name, proc->pid, (unsigned long long)commitMB, MEM_HOG_THRESHOLD_MB);
            Log("[MEM ANOMALY] %s", a->description);
        }
    }
    LeaveCriticalSection(&g_csData);

    /* GPU anomalies */
    if (g_gpuInfo.available && g_gpuInfo.utilization > 90) {
        static time_t lastGpuAlert = 0;
        if (time(NULL) - lastGpuAlert > 3600 && g_anomalyCount < MAX_ANOMALY_ALERTS) {
            AnomalyAlert *a = &g_anomalies[g_anomalyCount++];
            memset(a, 0, sizeof(*a));
            a->timestamp = time(NULL);
            a->type = ANOMALY_GPU_HOG;
            a->pid = 0;
            strncpy(a->procName, "GPU", sizeof(a->procName) - 1);
            a->value = g_gpuInfo.utilization;
            snprintf(a->description, sizeof(a->description),
                "GPU utilization %d%%, VRAM %I64u/%I64u MB",
                g_gpuInfo.utilization,
                (unsigned long long)(g_gpuInfo.dedicatedUsed / (1024 * 1024)),
                (unsigned long long)(g_gpuInfo.dedicatedTotal / (1024 * 1024)));
            Log("[GPU ANOMALY] %s", a->description);
            lastGpuAlert = time(NULL);
        }
    }
}

/* ============================================================================
 * Memory trimming
 * ============================================================================ */
BOOL TrimProcessWorkingSet(DWORD pid) {
    HANDLE hProcess = OpenProcess(
        PROCESS_SET_QUOTA | PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (!hProcess) return FALSE;
    BOOL result = EmptyWorkingSet(hProcess);
    if (!result)
        result = SetProcessWorkingSetSize(hProcess, (SIZE_T)-1, (SIZE_T)-1);
    CloseHandle(hProcess);
    return result;
}

static void TakeAction(void) {
    time_t now = time(NULL);
    if (now - g_tLastAction < ACTION_COOLDOWN_SEC) {
        Log("Action cooldown, skip (last action %I64d sec ago)",
            (long long)(now - g_tLastAction));
        return;
    }

    DWORD pageFileBefore = GetPageFileUsagePct();
    Log("======== Memory cleanup started ========");
    g_tLastAction = now;

    DWORD trimmed = 0, failed = 0, skipped = 0;
    DWORD foregroundPid = 0;
    HWND hForeground = GetForegroundWindow();
    if (hForeground) GetWindowThreadProcessId(hForeground, &foregroundPid);
    DWORD selfPid = GetCurrentProcessId();

    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32 pe;
        memset(&pe, 0, sizeof(pe));
        pe.dwSize = sizeof(PROCESSENTRY32);
        if (Process32First(hSnapshot, &pe)) {
            do {
                DWORD pid = pe.th32ProcessID;
                if (pid == 0 || pid == 4) { skipped++; continue; }
                if (pid == selfPid)      { skipped++; continue; }
                if (pid == foregroundPid) { skipped++; continue; }
                if (TrimProcessWorkingSet(pid)) trimmed++; else failed++;
            } while (Process32Next(hSnapshot, &pe));
        }
        CloseHandle(hSnapshot);
    }
    Log("  Result: %lu trimmed, %lu failed, %lu skipped", trimmed, failed, skipped);

    DWORD pageFileAfter = GetPageFileUsagePct();
    Log("Page file: %lu%% -> %lu%%", pageFileBefore, pageFileAfter);
    Log("======== Memory cleanup done ========");

    EnterCriticalSection(&g_csData);
    if (g_actionCount < MAX_ACTION_LOG) {
        ActionRecord *act = &g_actions[g_actionCount++];
        act->timestamp = now;
        act->pageFileBefore = pageFileBefore;
        act->pageFileAfter = pageFileAfter;
        act->trimmedCount = (int)trimmed;
        act->failedCount = (int)failed;
        snprintf(act->description, sizeof(act->description),
            "%lu trimmed, %lu failed: %lu%% -> %lu%%",
            trimmed, failed, pageFileBefore, pageFileAfter);
        AppendActionLog(act);
    }
    LeaveCriticalSection(&g_csData);

    UpdateLatestSnapshot();
}

void UpdateLatestSnapshot(void) {
    SampleProcessCpu();
    QueryGpuInfo();

    EnterCriticalSection(&g_csData);

    MemorySnapshot snap;
    CollectSnapshot(&snap);

    /* Ring buffer */
    if (g_snapshotCount < MAX_HISTORY_SNAPSHOTS) {
        memcpy(&g_snapshots[g_snapshotCount], &snap, sizeof(snap));
        g_snapshotCount++;
    } else {
        memmove(g_snapshots, g_snapshots + 1,
                (MAX_HISTORY_SNAPSHOTS - 1) * sizeof(MemorySnapshot));
        memcpy(&g_snapshots[MAX_HISTORY_SNAPSHOTS - 1], &snap, sizeof(snap));
    }
    memcpy(&g_latestSnapshot, &snap, sizeof(snap));

    LeaveCriticalSection(&g_csData);

    AppendSnapshot(&snap);
    UpdateAggregations(&snap);
    DetectAnomalies();
    TrackSuspiciousProcesses();
}

void CheckAndAct(void) {
    DWORD idleMs = GetIdleTimeMs();
    DWORD pageFilePct = GetPageFileUsagePct();

    if (g_bConsole) {
        Log("[Heartbeat] idle: %lu sec | page file: %lu%%",
            idleMs / 1000, pageFilePct);
    }

    if (idleMs >= IDLE_THRESHOLD_SEC * 1000 &&
        pageFilePct >= PAGE_FILE_THRESHOLD_PCT) {
        Log("Triggered: idle %lu sec, page file %lu%%",
            idleMs / 1000, pageFilePct);
        TakeAction();
    }

    UpdateLatestSnapshot();
}
