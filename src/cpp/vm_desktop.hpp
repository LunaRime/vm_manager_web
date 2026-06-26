/**
 * vm_desktop.hpp — C++ Desktop GUI application
 *
 * Win32 native window with dark theme, 6 tabs:
 *   Overview | Processes | Database | Charts | Anomalies | Suspicious
 *
 * Uses VMI18n for all user-facing strings (zh-CN / zh-TW / en-US).
 * All GDI rendering uses ANTIALIASED_QUALITY for clean CJK text.
 */
#ifndef VM_DESKTOP_HPP
#define VM_DESKTOP_HPP

#include "vm_bridge.hpp"
#include "vm_i18n.hpp"

#include <string>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

// ============================================================================
// VMCore — thin C++ wrapper over the C global state for desktop access
// ============================================================================
class VMCore {
public:
    static MemorySnapshot &Latest()       { return g_latestSnapshot; }
    static int            SnapshotCount() { return g_snapshotCount; }
    static MemorySnapshot &Snapshot(int i) { return g_snapshots[i]; }
    static int            ActionCount()   { return g_actionCount; }
    static ActionRecord   &Action(int i)  { return g_actions[i]; }
    static int            AnomalyCount()  { return g_anomalyCount; }
    static AnomalyAlert   &Anomaly(int i) { return g_anomalies[i]; }
    static int            SuspCount()     { return g_suspProcCount; }
    static SuspiciousProc &Susp(int i)    { return g_suspProcs[i]; }
    static int            CpuCount()      { return g_cpuSampleCount; }
    static CpuSample      &Cpu(int i)     { return g_cpuSamples[i]; }
    static GpuInfo        &Gpu()          { return g_gpuInfo; }

    /* Aggregation accessors */
    static AggBucket &Hourly(int i)  { return g_hourlyBuckets[i]; }
    static int        HourlyCount()  { return g_hourlyCount; }
    static AggBucket &Daily(int i)   { return g_dailyBuckets[i]; }
    static int        DailyCount()   { return g_dailyCount; }
    static AggBucket &Monthly(int i) { return g_monthlyBuckets[i]; }
    static int        MonthlyCount() { return g_monthlyCount; }
};

// ============================================================================
// VMDesktopApp — main desktop application
// ============================================================================
class VMDesktopApp {
public:
    VMDesktopApp();
    ~VMDesktopApp();

    int Run(HINSTANCE hInst, int nCmdShow);

private:
    /* ---- Window ---- */
    bool RegisterWindowClass();
    bool CreateMainWindow(int nCmdShow);
    void CreateChildControls();
    void LayoutChildren(int w, int h);

    /* ---- Message handling ---- */
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT HandleMessage(HWND, UINT, WPARAM, LPARAM);

    void OnCreate(HWND hwnd);
    void OnSize(int w, int h);
    void OnCommand(WORD id, WORD code, HWND ctrl);
    void OnNotify(NMHDR *nm);
    void OnTimer();
    void OnTrayIcon(WPARAM wp, LPARAM lp);
    void OnDestroy();

    /* ---- Tab switching ---- */
    enum Tab { TAB_OV = 0, TAB_PROC, TAB_DB, TAB_CHART, TAB_ANOM, TAB_SUSP, TAB_COUNT };
    void ShowTab(Tab t);

    /* ---- Data refresh ---- */
    void RefreshAll();
    void RefreshOverview();
    void RefreshProcessList();
    void RefreshDatabasePanel();
    void RefreshChart();
    void RefreshAnomalyList();
    void RefreshSuspiciousList();
    void RefreshStatusBar();

    /* ---- GDI drawing ---- */
    static LRESULT CALLBACK ChartWndProc(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK OverviewWndProc(HWND, UINT, WPARAM, LPARAM);
    void DrawOverviewCards(HDC hdc, RECT rc);
    void DrawLineChart(HDC hdc, RECT rc);
    void DrawBarChart(HDC hdc, RECT rc);

    /* ---- ListView helpers (Unicode) ---- */
    HWND CreateLV(int x, int y, int w, int h,
                  const char **headers, int *widths, int nCols, int id);
    void AddLV(HWND lv, int row, int col, const WCHAR *text);
    void ClearLV(HWND lv);

    /* ---- System tray ---- */
    void AddTrayIcon();
    void RemoveTrayIcon();
    void ShowTrayMenu();

    /* ---- Database ---- */
    void ExportDatabaseCSV();

    /* ---- Formatting ---- */
    static std::string FmtMB(ULONGLONG bytes);
    static std::string FmtTime(time_t t);
    static std::string FmtDuration(time_t sec);

    /* ---- Members ---- */
    HINSTANCE m_hInst;
    HWND      m_hWnd;
    HFONT     m_hFont;
    HFONT     m_hTitleFont;
    HFONT     m_hMonoFont;

    /* Child windows */
    HWND m_hTab;
    HWND m_hBtnCleanup;
    HWND m_hBtnExport;
    HWND m_hBtnServer;
    HWND m_hBtnExit;

    /* Tab pages */
    HWND m_hOverviewPane;
    HWND m_hProcList;
    HWND m_hDbPanel;
    HWND m_hChartArea;
    HWND m_hAnomList;
    HWND m_hSuspList;
    HWND m_hActionLog;

    /* Chart controls */
    HWND m_hCboRange;
    HWND m_hLblRange;
    HWND m_hBtnChartMode;

    /* Status bar */
    HWND m_hStatus;

    /* State */
    Tab  m_activeTab;
    int  m_chartRange;    /* CHART_DAY .. CHART_YEAR */
    int  m_chartMode;     /* 0=line, 1=bar */
    bool m_running;

    /* Dark theme colors */
    static const COLORREF kBg;
    static const COLORREF kCard;
    static const COLORREF kCard2;
    static const COLORREF kBorder;
    static const COLORREF kBorderLt;
    static const COLORREF kText;
    static const COLORREF kText2;
    static const COLORREF kMuted;
    static const COLORREF kAccent;
    static const COLORREF kGreen;
    static const COLORREF kRed;
    static const COLORREF kOrange;
    static const COLORREF kYellow;
    static const COLORREF kPfColor;
    static const COLORREF kPhColor;

    /* Window class names (Unicode) */
    static const WCHAR *kMainClass;
    static const WCHAR *kChartClass;
    static const WCHAR *kOverviewClass;

    /* Timer ID */
    static const UINT_PTR kRefreshTimer = 1001;
    static const UINT   kRefreshMs = 3000;
    static const UINT   kTrayUID = 1;
    static const UINT   kTrayMsg = WM_APP + 100;

    /* Control IDs */
    enum CtlId {
        ID_TAB        = 2001,
        ID_BTN_CLEANUP= 2002,
        ID_BTN_EXPORT = 2003,
        ID_BTN_EXIT   = 2004,
        ID_BTN_SERVER = 2005,
        ID_LIST_PROC  = 2101,
        ID_LIST_ANOM  = 2102,
        ID_LIST_SUSP  = 2103,
        ID_LIST_ACTS  = 2104,
        ID_DB_PANEL   = 2201,
        ID_CHART_AREA = 2301,
        ID_OV_PANE    = 2401,
        ID_CBO_RANGE  = 2501,
        ID_BTN_CHART  = 2502,
    };

    enum MenuId {
        IDM_SHOW    = 3001,
        IDM_CLEANUP = 3002,
        IDM_EXPORT  = 3003,
        IDM_EXIT    = 3004,
    };
};

#endif /* VM_DESKTOP_HPP */
