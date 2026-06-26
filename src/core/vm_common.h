/**
 * vm_common.h — 共享数据结构、常量、全局变量声明
 *
 * VM Manager v4.0 — 模块化重构版
 * 桌面 GUI + Web 仪表盘 + 加密数据库 + 异常检测
 */
#ifndef VM_COMMON_H
#define VM_COMMON_H

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif

#define __USE_MINGW_ANSI_STDIO 1
#define WIN32_LEAN_AND_MEAN

/* MinGW 6.3.0 compatibility: define _WIN32_IE for commctrl features */
#ifndef _WIN32_IE
#define _WIN32_IE 0x0600
#endif

#include <windows.h>
#include <wincrypt.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <commctrl.h>
#include <objbase.h>         /* IID/REFIID for shellapi.h */
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdarg.h>

/* i18n */
#include "vm_locale.h"

/* ============================================================================
 * MinGW 6.3.0 commctrl.h compatibility — manually define missing constants
 * ============================================================================ */
#ifndef LVS_EX_DOUBLEBUFFER
#define LVS_EX_DOUBLEBUFFER  0x00010000
#endif
#ifndef LVS_EX_FULLROWSELECT
#define LVS_EX_FULLROWSELECT 0x00000020
#endif
#ifndef WC_TABCONTROLA
#define WC_TABCONTROLA       "SysTabControl32"
#endif
#ifndef WC_LISTVIEWA
#define WC_LISTVIEWA         "SysListView32"
#endif
#ifndef STATUSCLASSNAMEA
#define STATUSCLASSNAMEA     "msctls_statusbar32"
#endif

#ifndef IDI_INFORMATION
#define IDI_INFORMATION      MAKEINTRESOURCEA(32516)
#endif

/* ICC constants */
#ifndef ICC_TAB_CLASSES
#define ICC_TAB_CLASSES      0x00000008
#endif
#ifndef ICC_LISTVIEW_CLASSES
#define ICC_LISTVIEW_CLASSES 0x00000001
#endif

/* ============================================================================
 * MinGW 6.3.0 missing commctrl macros — define manually
 * ============================================================================ */

/* TabCtrl macros */
#ifndef TabCtrl_InsertItemA
#define TabCtrl_InsertItemA(hwnd, iItem, pitem) \
    (int)SendMessageA((hwnd), TCM_INSERTITEMA, (WPARAM)(iItem), (LPARAM)(pitem))
#endif
#ifndef TabCtrl_GetCurSel
#define TabCtrl_GetCurSel(hwnd) \
    (int)SendMessageA((hwnd), TCM_GETCURSEL, 0, 0)
#endif

/* InitCommonControlsEx fallback for ANSI-only MinGW */
#ifndef InitCommonControlsEx
#define InitCommonControlsEx(ptr) InitCommonControls()
#endif

/* LVM constants that might be missing */
#ifndef LVM_SETEXTENDEDLISTVIEWSTYLE
#define LVM_SETEXTENDEDLISTVIEWSTYLE (LVM_FIRST + 54)
#endif

/* ============================================================================
 * MinGW 6.3.0 ListView helpers — implemented as inline functions in
 * vm_desktop.c to avoid macro expansion issues with commctrl.h
 * ============================================================================ */

/* ============================================================================
 * DPAPI 常量 (MinGW 6.3.0 wincrypt.h 可能缺失)
 * ============================================================================ */
#ifndef CRYPTPROTECT_LOCAL_MACHINE
#define CRYPTPROTECT_LOCAL_MACHINE  0x04
#endif
#ifndef CRYPTPROTECT_UI_FORBIDDEN
#define CRYPTPROTECT_UI_FORBIDDEN   0x01
#endif

/* ============================================================================
 * 可配置参数
 * ============================================================================ */
#define CHECK_INTERVAL_SEC       30
#define IDLE_THRESHOLD_SEC       300
#define PAGE_FILE_THRESHOLD_PCT  85
#define HTTP_PORT_START          18080
#define HTTP_PORT_END            18089
#define MAX_TOP_PROCESSES        20
#define MAX_HISTORY_SNAPSHOTS    2880
#define MAX_ACTION_LOG           200
#define ACTION_COOLDOWN_SEC      600
#define LOG_FILE_NAME            "vm_manager.log"
#define DB_FILE_NAME             "vm_data.db"
#define DESKTOP_REFRESH_MS       3000

/* 聚合历史 — 用于长期图表 */
#define MAX_HOURLY_POINTS        168    /* 7天 × 24小时 */
#define MAX_DAILY_POINTS         90     /* 90天 */
#define MAX_MONTHLY_POINTS       12     /* 12个月 */

/* 异常检测阈值 */
#define CPU_HOG_THRESHOLD_PCT    50
#define CPU_ANOMALY_SAMPLES      3
#define MEM_HOG_THRESHOLD_MB     4096
#define GPU_HOG_VRAM_PCT         50
#define MAX_ANOMALY_ALERTS       100
#define MAX_CPU_SAMPLE_PIDS      512

/* 可疑进程检测 */
#define SUSPICIOUS_GROWTH_MB_PER_SEC  10   /* 每秒内存增长 > 10MB */
#define SUSPICIOUS_COMMIT_MIN_MB      100  /* 至少 100MB 才触发 */
#define MAX_SUSPICIOUS_PROCS          20

/* ============================================================================
 * 数据结构
 * ============================================================================ */

/* 进程信息 */
typedef struct {
    DWORD  pid;
    SIZE_T commitSize;       /* 提交大小 (PrivateUsage) */
    SIZE_T workingSet;       /* 工作集 */
    SIZE_T prevCommit;       /* 上次采样的 commit (用于增长检测) */
    time_t prevSampleTime;   /* 上次采样时间 */
    double growthRateMBps;   /* 内存增长速率 MB/s */
    char   name[MAX_PATH];
} ProcessInfo;

/* 内存快照 */
typedef struct {
    time_t      timestamp;
    DWORD       pageFilePct;
    DWORD       physLoad;
    ULONGLONG   totalPhys;
    ULONGLONG   availPhys;
    ULONGLONG   totalPageFile;
    ULONGLONG   availPageFile;
    DWORD       idleSeconds;
    int         numProcesses;
    ProcessInfo topProcesses[MAX_TOP_PROCESSES];
} MemorySnapshot;

/* 操作记录 */
typedef struct {
    time_t  timestamp;
    DWORD   pageFileBefore;
    DWORD   pageFileAfter;
    int     trimmedCount;
    int     failedCount;
    char    description[256];
} ActionRecord;

/* CPU 采样点 */
typedef struct {
    DWORD     pid;
    ULONGLONG prevKernelTime;
    ULONGLONG prevUserTime;
    ULONGLONG prevSampleTick;
    double    cpuPercent;
    int       consecutiveHigh;
} CpuSample;

/* GPU 信息 */
typedef struct {
    int    utilization;
    SIZE_T dedicatedUsed;
    SIZE_T dedicatedTotal;
    SIZE_T sharedUsed;
    SIZE_T sharedTotal;
    char   name[128];
    BOOL   available;
} GpuInfo;

/* 聚合数据点 (用于长期图表) */
typedef struct {
    time_t  bucketStart;     /* 桶起始时间 */
    double  pfSum;           /* 页面文件% 总和 */
    double  pfMax;           /* 页面文件% 最大值 */
    double  phSum;           /* 物理内存% 总和 */
    double  phMax;           /* 物理内存% 最大值 */
    int     sampleCount;
} AggBucket;

/* 异常类型 */
typedef enum {
    ANOMALY_CPU_HOG = 0,
    ANOMALY_MEM_LEAK,
    ANOMALY_MEM_HOG,
    ANOMALY_GPU_HOG,
    ANOMALY_SUSPICIOUS,      /* 可疑程序 */
} AnomalyType;

/* 异常警报 */
typedef struct {
    time_t      timestamp;
    AnomalyType type;
    DWORD       pid;
    char        procName[MAX_PATH];
    double      value;
    SIZE_T      commitMB;
    char        description[256];
} AnomalyAlert;

/* 可疑进程追踪 */
typedef struct {
    DWORD  pid;
    char   name[MAX_PATH];
    SIZE_T firstCommit;      /* 首次检测时的 commit */
    SIZE_T lastCommit;       /* 最新 commit */
    time_t firstSeen;        /* 首次检测时间 */
    time_t lastSeen;         /* 最新检测时间 */
    double peakGrowthRate;   /* 峰值增长速率 MB/s */
    int    alertCount;       /* 触发警报次数 */
} SuspiciousProc;

/* ============================================================================
 * PDH 动态加载类型 (GPU 监控)
 * ============================================================================ */
typedef HANDLE  PDH_HQUERY;
typedef HANDLE  PDH_HCOUNTER;
typedef LONG    PDH_STATUS;

#define PDH_MORE_DATA           0x800007D2L
#define PDH_FMT_DOUBLE          0x00000200

typedef struct _PDH_FMT_COUNTERVALUE_ITEM_A {
    LPSTR   szName;
    struct {
        LONG     CStatus;
        double   doubleValue;
    } FmtValue;
} PDH_FMT_COUNTERVALUE_ITEM_A;

typedef PDH_STATUS (WINAPI *PFN_PdhOpenQueryA)(LPCSTR, DWORD_PTR, PDH_HQUERY*);
typedef PDH_STATUS (WINAPI *PFN_PdhAddCounterA)(PDH_HQUERY, LPCSTR, DWORD_PTR, PDH_HCOUNTER*);
typedef PDH_STATUS (WINAPI *PFN_PdhAddEnglishCounterA)(PDH_HQUERY, LPCSTR, DWORD_PTR, PDH_HCOUNTER*);
typedef PDH_STATUS (WINAPI *PFN_PdhCollectQueryData)(PDH_HQUERY);
typedef PDH_STATUS (WINAPI *PFN_PdhGetFormattedCounterArrayA)(PDH_HCOUNTER, DWORD, DWORD*, DWORD*, PDH_FMT_COUNTERVALUE_ITEM_A*);
typedef PDH_STATUS (WINAPI *PFN_PdhCloseQuery)(PDH_HQUERY);

/* ============================================================================
 * 全局状态 — 跨模块共享 (定义在 vm_engine.c)
 * ============================================================================ */
extern HANDLE  g_hLogFile;
extern BOOL    g_bRunning;
extern BOOL    g_bConsole;
extern BOOL    g_bDesktop;           /* 桌面 GUI 模式 */
extern time_t  g_tLastAction;
extern time_t  g_tStartTime;
extern int     g_httpPort;

/* 数据存储 (受 g_csData 保护) */
extern CRITICAL_SECTION g_csData;
extern MemorySnapshot  g_snapshots[MAX_HISTORY_SNAPSHOTS];
extern int             g_snapshotCount;
extern MemorySnapshot  g_latestSnapshot;
extern ActionRecord    g_actions[MAX_ACTION_LOG];
extern int             g_actionCount;

/* 聚合缓冲区 */
extern AggBucket  g_hourlyBuckets[MAX_HOURLY_POINTS];
extern int        g_hourlyCount;
extern AggBucket  g_dailyBuckets[MAX_DAILY_POINTS];
extern int        g_dailyCount;
extern AggBucket  g_monthlyBuckets[MAX_MONTHLY_POINTS];
extern int        g_monthlyCount;

/* 数据库 */
extern HANDLE  g_hDbFile;

/* CPU/GPU/异常 */
extern CpuSample     g_cpuSamples[MAX_CPU_SAMPLE_PIDS];
extern int           g_cpuSampleCount;
extern GpuInfo       g_gpuInfo;
extern AnomalyAlert  g_anomalies[MAX_ANOMALY_ALERTS];
extern int           g_anomalyCount;
extern DWORD         g_numProcessors;
extern ULONGLONG     g_lastCpuSampleTick;

/* 可疑进程追踪 */
extern SuspiciousProc g_suspProcs[MAX_SUSPICIOUS_PROCS];
extern int            g_suspProcCount;

/* PDH 函数指针 (GPU) */
extern PFN_PdhOpenQueryA                g_pfnPdhOpenQueryA;
extern PFN_PdhAddCounterA               g_pfnPdhAddCounterA;
extern PFN_PdhAddEnglishCounterA        g_pfnPdhAddEnglishCounterA;
extern PFN_PdhCollectQueryData          g_pfnPdhCollectQueryData;
extern PFN_PdhGetFormattedCounterArrayA  g_pfnPdhGetFormattedCounterArrayA;
extern PFN_PdhCloseQuery                g_pfnPdhCloseQuery;
extern HMODULE g_hPdhDll;
extern PDH_HQUERY    g_hPdhQuery;
extern PDH_HCOUNTER  g_hPdhCounterGpu;
extern BOOL          g_bPdhAvailable;

/* HTTP 服务器线程 */
extern HANDLE  g_hHttpThread;

/* 托盘图标 */
#define WM_TRAYICON   (WM_APP + 100)
#define TRAY_UID      1
#define TRAY_TIP      L"VM Manager — 虚拟内存监控"

/* 桌面窗口控件 ID */
#define IDC_TAB          1001
#define IDC_BTN_START    2001
#define IDC_BTN_STOP     2002
#define IDC_BTN_CLEANUP  2003
#define IDC_BTN_EXIT     2004
#define IDC_LIST_PROC    3001
#define IDC_LIST_ANOMALY 3002
#define IDC_LIST_ACTIONS 3003
#define IDC_COMBO_RANGE  4001
#define IDC_CHART_AREA   5001
#define IDT_DESKTOP_REFRESH  6001

/* 桌面菜单 */
#define IDM_SHOW        7001
#define IDM_CLEANUP     7002
#define IDM_EXIT        7003

/* 图表时间范围 */
#define CHART_DAY        0
#define CHART_WEEK       1
#define CHART_MONTH      2
#define CHART_YEAR       3

/* ============================================================================
 * 跨模块函数声明
 * ============================================================================ */

/* vm_engine.c */
void Log(const char *format, ...);
BOOL InitLogFile(void);
BOOL EnableDebugPrivilege(void);
void CollectSnapshot(MemorySnapshot *snap);
void SampleProcessCpu(void);
void QueryGpuInfo(void);
void InitGpuMonitoring(void);
void ShutdownGpuMonitoring(void);
void DetectAnomalies(void);
BOOL TrimProcessWorkingSet(DWORD pid);
DWORD GetPageFileUsagePct(void);
DWORD GetIdleTimeMs(void);
void UpdateLatestSnapshot(void);
void CheckAndAct(void);
void TrackSuspiciousProcesses(void);

/* vm_db.c */
BOOL InitDatabase(void);
BOOL LoadDatabase(void);
BOOL AppendSnapshot(MemorySnapshot *snap);
BOOL AppendActionLog(ActionRecord *action);
BOOL EncryptData(const BYTE *plainData, DWORD plainLen, BYTE **cipherData, DWORD *cipherLen);
BOOL DecryptData(const BYTE *cipherData, DWORD cipherLen, BYTE **plainData, DWORD *plainLen);

/* vm_http.c */
DWORD WINAPI HttpServerThread(LPVOID lpParam);

/* vm_desktop.c */
int RunDesktop(void);

/* main.c / entry */
void UpdateAggregations(MemorySnapshot *snap);

#endif /* VM_COMMON_H */
