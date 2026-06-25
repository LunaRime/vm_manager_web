/**
 * vm_desktop.c — Desktop GUI with system tray, tabs, and GDI charts.
 *                Uses raw SendMessage for ListView ops (MinGW 6.3.0 compat).
 */
#include "vm_common.h"

/* ============================================================================
 * Forward declarations
 * ============================================================================ */
static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
static LRESULT CALLBACK ChartProc(HWND, UINT, WPARAM, LPARAM);
static void     DrawGdiChart(HDC, RECT, int);
static void     RefreshDesktopData(HWND);
static void     AddTrayIcon(HWND);
static void     RemoveTrayIcon(HWND);
static void     ShowTrayMenu(HWND);

/* ============================================================================
 * Raw SendMessage wrappers for ListView (instead of MinGW macros)
 * ============================================================================ */
static int LvInsertItem(HWND lv, const LVITEMA *item) {
    return (int)SendMessageA(lv, LVM_INSERTITEMA, 0, (LPARAM)item);
}
static int LvInsertColumn(HWND lv, int iCol, const LV_COLUMNA *col) {
    return (int)SendMessageA(lv, LVM_INSERTCOLUMNA, (WPARAM)iCol, (LPARAM)col);
}
static void LvSetItemText(HWND lv, int i, int iSubItem, const char *text) {
    LVITEMA lvi;
    memset(&lvi, 0, sizeof(lvi));
    lvi.iSubItem = iSubItem;
    lvi.pszText = (LPSTR)text;
    SendMessageA(lv, LVM_SETITEMTEXTA, (WPARAM)i, (LPARAM)&lvi);
}
static void LvDeleteAll(HWND lv) {
    SendMessageA(lv, LVM_DELETEALLITEMS, 0, 0);
}
static void LvSetExStyle(HWND lv, DWORD ex) {
    SendMessageA(lv, LVM_SETEXTENDEDLISTVIEWSTYLE, 0, (LPARAM)ex);
}

/* TabCtrl raw wrappers */
static int TcInsertItem(HWND tc, int i, const TCITEMA *tci) {
    return (int)SendMessageA(tc, TCM_INSERTITEMA, (WPARAM)i, (LPARAM)tci);
}
static int TcGetCurSel(HWND tc) {
    return (int)SendMessageA(tc, TCM_GETCURSEL, 0, 0);
}

/* ============================================================================
 * Globals for desktop UI
 * ============================================================================ */
static HWND   g_hDesktopWnd    = NULL;
static HWND   g_hTab           = NULL;
static HWND   g_hChartArea     = NULL;
static HWND   g_hProcList      = NULL;
static HWND   g_hAnomalyList   = NULL;
static HWND   g_hSuspList      = NULL;
static HWND   g_hActionList    = NULL;
static HWND   g_hBtnCleanup    = NULL;
static HWND   g_hComboRange    = NULL;
static HWND   g_hStatusBar     = NULL;
static int    g_chartRange     = CHART_WEEK;
static HFONT  g_hGuiFont       = NULL;

/* ============================================================================
 * Color scheme (dark theme)
 * ============================================================================ */
#define CLR_BG          RGB(13, 17, 23)
#define CLR_CARD        RGB(22, 27, 34)
#define CLR_BORDER      RGB(48, 54, 61)
#define CLR_TEXT        RGB(201, 209, 217)
#define CLR_MUTED       RGB(139, 148, 158)
#define CLR_ACCENT      RGB(88, 166, 255)
#define CLR_GREEN       RGB(63, 185, 80)
#define CLR_RED         RGB(248, 81, 73)
#define CLR_YELLOW      RGB(210, 153, 34)
#define CLR_CHART_PF    RGB(88, 166, 255)
#define CLR_CHART_PH    RGB(63, 185, 80)

/* ============================================================================
 * Formatting helpers
 * ============================================================================ */
static void FmtMB(char *out, int outSize, ULONGLONG bytes) {
    if (bytes >= 1024ULL * 1024 * 1024)
        snprintf(out, outSize, "%.1f GB", bytes / (1024.0 * 1024.0 * 1024.0));
    else
        snprintf(out, outSize, "%I64u MB", (unsigned long long)(bytes / (1024 * 1024)));
}
static void FmtTime(char *out, int outSize, time_t t) {
    struct tm *tmInfo = localtime(&t);
    strftime(out, outSize, "%Y-%m-%d %H:%M", tmInfo);
}
static void FmtDuration(char *out, int outSize, time_t sec) {
    if (sec < 60) snprintf(out, outSize, "%I64ds", (long long)sec);
    else if (sec < 3600) snprintf(out, outSize, "%I64dm", (long long)(sec / 60));
    else snprintf(out, outSize, "%I64dh %I64dm", (long long)(sec / 3600), (long long)((sec % 3600) / 60));
}

/* ============================================================================
 * System tray icon
 * ============================================================================ */
static void AddTrayIcon(HWND hwnd) {
    NOTIFYICONDATAA nid;
    memset(&nid, 0, sizeof(nid));
    nid.cbSize           = sizeof(NOTIFYICONDATAA);
    nid.hWnd             = hwnd;
    nid.uID              = TRAY_UID;
    nid.uFlags           = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon            = LoadIcon(NULL, IDI_INFORMATION);
    lstrcpyA((LPSTR)nid.szTip, "VM Manager — Memory Monitor");
    Shell_NotifyIconA(NIM_ADD, &nid);
}
static void RemoveTrayIcon(HWND hwnd) {
    NOTIFYICONDATAA nid;
    memset(&nid, 0, sizeof(nid));
    nid.cbSize = sizeof(NOTIFYICONDATAA);
    nid.hWnd   = hwnd;
    nid.uID    = TRAY_UID;
    Shell_NotifyIconA(NIM_DELETE, &nid);
}
static void ShowTrayMenu(HWND hwnd) {
    HMENU hMenu = CreatePopupMenu();
    AppendMenuA(hMenu, MF_STRING, IDM_SHOW,    "&Show Window");
    AppendMenuA(hMenu, MF_STRING, IDM_CLEANUP, "&Manual Cleanup");
    AppendMenuA(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuA(hMenu, MF_STRING, IDM_EXIT,    "E&xit");
    POINT pt; GetCursorPos(&pt);
    SetForegroundWindow(hwnd);
    TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
    DestroyMenu(hMenu);
}

/* ============================================================================
 * GDI Chart drawing
 * ============================================================================ */
static void DrawGdiChart(HDC hdc, RECT rc, int chartRange) {
    int w = rc.right - rc.left, h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0) return;

    HBRUSH hBg = CreateSolidBrush(CLR_CARD);
    FillRect(hdc, &rc, hBg);
    DeleteObject(hBg);

    int padTop = 30, padBottom = 35, padLeft = 55, padRight = 30;
    int pw = w - padLeft - padRight, ph = h - padTop - padBottom;
    if (pw <= 0 || ph <= 0) return;

    AggBucket *buckets = NULL;
    int count = 0, maxTake = 30;

    EnterCriticalSection(&g_csData);
    switch (chartRange) {
    case CHART_DAY:   buckets = g_hourlyBuckets; count = g_hourlyCount; maxTake = 24;  break;
    case CHART_WEEK:  buckets = g_dailyBuckets;  count = g_dailyCount;  maxTake = 7;   break;
    case CHART_MONTH: buckets = g_dailyBuckets;  count = g_dailyCount;  maxTake = 30;  break;
    case CHART_YEAR:  buckets = g_monthlyBuckets; count = g_monthlyCount; maxTake = 12; break;
    }

    int take = count < maxTake ? count : maxTake;
    int start = count - take;
    if (start < 0) { start = 0; take = count; }

    AggBucket *plot = (AggBucket *)HeapAlloc(GetProcessHeap(), 0, take * sizeof(AggBucket));
    if (plot) memcpy(plot, buckets + start, take * sizeof(AggBucket));
    LeaveCriticalSection(&g_csData);
    if (!plot || take < 2) { if (plot) HeapFree(GetProcessHeap(), 0, plot); return; }

    /* Grid lines */
    HPEN hGridPen = CreatePen(PS_SOLID, 1, CLR_BORDER);
    HPEN hOldPen = (HPEN)SelectObject(hdc, hGridPen);
    int g;
    for (g = 0; g <= 100; g += 25) {
        int y = padTop + ph - (ph * g / 100);
        MoveToEx(hdc, padLeft, y, NULL);
        LineTo(hdc, padLeft + pw, y);
        char lbl[8]; snprintf(lbl, sizeof(lbl), "%d%%", g);
        SetTextColor(hdc, CLR_MUTED); SetBkMode(hdc, TRANSPARENT);
        RECT tr = { 2, y - 8, padLeft - 4, y + 8 };
        DrawTextA(hdc, lbl, -1, &tr, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    }
    SelectObject(hdc, hOldPen); DeleteObject(hGridPen);

    /* Axes */
    HPEN hAxisPen = CreatePen(PS_SOLID, 1, CLR_MUTED);
    hOldPen = (HPEN)SelectObject(hdc, hAxisPen);
    MoveToEx(hdc, padLeft, padTop, NULL);
    LineTo(hdc, padLeft, padTop + ph);
    LineTo(hdc, padLeft + pw, padTop + ph);
    SelectObject(hdc, hOldPen); DeleteObject(hAxisPen);

    /* Page File (solid blue) */
    HPEN hPenPF = CreatePen(PS_SOLID, 2, CLR_CHART_PF);
    hOldPen = (HPEN)SelectObject(hdc, hPenPF);
    int i;
    for (i = 0; i < take; i++) {
        double pfVal = plot[i].sampleCount > 0 ? plot[i].pfSum / plot[i].sampleCount : 0;
        int x = padLeft + (int)((double)i / (take - 1) * pw);
        int y = padTop + ph - (int)(pfVal / 100.0 * ph);
        if (y < padTop) y = padTop;
        if (y > padTop + ph) y = padTop + ph;
        if (i == 0) MoveToEx(hdc, x, y, NULL); else LineTo(hdc, x, y);
    }
    SelectObject(hdc, hOldPen); DeleteObject(hPenPF);

    /* Physical Memory (dashed green) */
    HPEN hPenPH = CreatePen(PS_DASH, 2, CLR_CHART_PH);
    hOldPen = (HPEN)SelectObject(hdc, hPenPH);
    for (i = 0; i < take; i++) {
        double phVal = plot[i].sampleCount > 0 ? plot[i].phSum / plot[i].sampleCount : 0;
        int x = padLeft + (int)((double)i / (take - 1) * pw);
        int y = padTop + ph - (int)(phVal / 100.0 * ph);
        if (y < padTop) y = padTop;
        if (y > padTop + ph) y = padTop + ph;
        if (i == 0) MoveToEx(hdc, x, y, NULL); else LineTo(hdc, x, y);
    }
    SelectObject(hdc, hOldPen); DeleteObject(hPenPH);

    /* X-axis labels */
    SetTextColor(hdc, CLR_MUTED); SetBkMode(hdc, TRANSPARENT);
    for (i = 0; i < take; i++) {
        if (take > 12 && i % (take / 6) != 0 && i != take - 1) continue;
        int x = padLeft + (int)((double)i / (take - 1) * pw);
        char lbl[32];
        struct tm *tmInfo = localtime(&plot[i].bucketStart);
        if (chartRange == CHART_DAY) strftime(lbl, sizeof(lbl), "%H:%M", tmInfo);
        else if (chartRange == CHART_YEAR) strftime(lbl, sizeof(lbl), "%b", tmInfo);
        else strftime(lbl, sizeof(lbl), "%m/%d", tmInfo);
        RECT tr = { x - 25, padTop + ph + 4, x + 25, padTop + ph + 20 };
        DrawTextA(hdc, lbl, -1, &tr, DT_CENTER | DT_TOP | DT_SINGLELINE);
    }

    /* Legend */
    RECT legPf = { padLeft, 4, padLeft + 80, 20 }, legPh = { padLeft + 90, 4, padLeft + 170, 20 };
    HBRUSH hPfBrush = CreateSolidBrush(CLR_CHART_PF);
    FillRect(hdc, &legPf, hPfBrush); DeleteObject(hPfBrush);
    SetTextColor(hdc, CLR_TEXT);
    RECT legPfT = { padLeft + 14, 4, padLeft + 80, 20 };
    DrawTextA(hdc, "Page File", -1, &legPfT, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    HBRUSH hPhBrush = CreateSolidBrush(CLR_CHART_PH);
    FillRect(hdc, &legPh, hPhBrush); DeleteObject(hPhBrush);
    RECT legPhT = { padLeft + 104, 4, padLeft + 170, 20 };
    DrawTextA(hdc, "Physical", -1, &legPhT, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    HeapFree(GetProcessHeap(), 0, plot);
}

/* ============================================================================
 * Chart window procedure
 * ============================================================================ */
static LRESULT CALLBACK ChartProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc; GetClientRect(hwnd, &rc);
        DrawGdiChart(hdc, rc, g_chartRange);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND: return 1;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

/* ============================================================================
 * UI update functions
 * ============================================================================ */
static void UpdateProcessList(HWND hwnd) {
    if (!g_hProcList) return;
    LvDeleteAll(g_hProcList);

    EnterCriticalSection(&g_csData);
    MemorySnapshot *s = &g_latestSnapshot;
    int i;
    for (i = 0; i < s->numProcesses; i++) {
        ProcessInfo *p = &s->topProcesses[i];
        char rank[8], pid[12], commit[32], ws[32], growth[32];
        snprintf(rank,   sizeof(rank),   "%d", i + 1);
        snprintf(pid,    sizeof(pid),    "%lu", p->pid);
        FmtMB(commit, sizeof(commit), p->commitSize);
        FmtMB(ws,     sizeof(ws),     p->workingSet);
        if (p->growthRateMBps > 0.01)
            snprintf(growth, sizeof(growth), "+%.1f MB/s", p->growthRateMBps);
        else
            strcpy(growth, "-");

        LVITEMA item;
        memset(&item, 0, sizeof(item));
        item.mask = LVIF_TEXT;
        item.iItem = i;
        item.pszText = rank;
        LvInsertItem(g_hProcList, &item);
        LvSetItemText(g_hProcList, i, 1, pid);
        LvSetItemText(g_hProcList, i, 2, p->name);
        LvSetItemText(g_hProcList, i, 3, commit);
        LvSetItemText(g_hProcList, i, 4, ws);
        LvSetItemText(g_hProcList, i, 5, growth);
    }
    LeaveCriticalSection(&g_csData);
}

static void UpdateAnomalyList(HWND hwnd) {
    if (!g_hAnomalyList) return;
    LvDeleteAll(g_hAnomalyList);

    EnterCriticalSection(&g_csData);
    int i;
    for (i = 0; i < g_anomalyCount; i++) {
        AnomalyAlert *a = &g_anomalies[i];
        char timeStr[32], typeStr[16], pidStr[12], valueStr[32];
        FmtTime(timeStr, sizeof(timeStr), a->timestamp);
        switch (a->type) {
        case ANOMALY_CPU_HOG:    strcpy(typeStr, "CPU Hog");    break;
        case ANOMALY_MEM_HOG:    strcpy(typeStr, "Mem Hog");    break;
        case ANOMALY_MEM_LEAK:   strcpy(typeStr, "Mem Leak");   break;
        case ANOMALY_GPU_HOG:    strcpy(typeStr, "GPU Hog");    break;
        case ANOMALY_SUSPICIOUS: strcpy(typeStr, "Suspicious"); break;
        default:                 strcpy(typeStr, "Unknown");    break;
        }
        snprintf(pidStr, sizeof(pidStr), "%lu", a->pid);
        snprintf(valueStr, sizeof(valueStr), "%.1f", a->value);

        LVITEMA item;
        memset(&item, 0, sizeof(item));
        item.mask = LVIF_TEXT; item.iItem = i; item.pszText = timeStr;
        LvInsertItem(g_hAnomalyList, &item);
        LvSetItemText(g_hAnomalyList, i, 1, typeStr);
        LvSetItemText(g_hAnomalyList, i, 2, pidStr);
        LvSetItemText(g_hAnomalyList, i, 3, a->procName);
        LvSetItemText(g_hAnomalyList, i, 4, valueStr);
        LvSetItemText(g_hAnomalyList, i, 5, a->description);
    }
    LeaveCriticalSection(&g_csData);
}

static void UpdateSuspiciousList(HWND hwnd) {
    if (!g_hSuspList) return;
    LvDeleteAll(g_hSuspList);

    EnterCriticalSection(&g_csData);
    int i;
    for (i = 0; i < g_suspProcCount; i++) {
        SuspiciousProc *sp = &g_suspProcs[i];
        char pidStr[12], firstMB[32], lastMB[32], growthMB[32];
        char rateStr[32], firstSeen[32], lastSeen[32], alerts[8];
        snprintf(pidStr, sizeof(pidStr), "%lu", sp->pid);
        FmtMB(firstMB, sizeof(firstMB), sp->firstCommit);
        FmtMB(lastMB, sizeof(lastMB), sp->lastCommit);
        SIZE_T gb = sp->lastCommit > sp->firstCommit ? (sp->lastCommit - sp->firstCommit) : 0;
        FmtMB(growthMB, sizeof(growthMB), gb);
        snprintf(rateStr, sizeof(rateStr), "%.1f MB/s", sp->peakGrowthRate);
        FmtTime(firstSeen, sizeof(firstSeen), sp->firstSeen);
        FmtTime(lastSeen, sizeof(lastSeen), sp->lastSeen);
        snprintf(alerts, sizeof(alerts), "%d", sp->alertCount);

        LVITEMA item;
        memset(&item, 0, sizeof(item));
        item.mask = LVIF_TEXT; item.iItem = i; item.pszText = pidStr;
        LvInsertItem(g_hSuspList, &item);
        LvSetItemText(g_hSuspList, i, 1, sp->name);
        LvSetItemText(g_hSuspList, i, 2, firstMB);
        LvSetItemText(g_hSuspList, i, 3, lastMB);
        LvSetItemText(g_hSuspList, i, 4, growthMB);
        LvSetItemText(g_hSuspList, i, 5, rateStr);
        LvSetItemText(g_hSuspList, i, 6, firstSeen);
        LvSetItemText(g_hSuspList, i, 7, lastSeen);
        LvSetItemText(g_hSuspList, i, 8, alerts);
    }
    LeaveCriticalSection(&g_csData);
}

static void UpdateActionList(HWND hwnd) {
    if (!g_hActionList) return;
    LvDeleteAll(g_hActionList);

    EnterCriticalSection(&g_csData);
    int i;
    for (i = 0; i < g_actionCount; i++) {
        ActionRecord *a = &g_actions[i];
        char timeStr[32], bfStr[8], afStr[8], tcStr[8], fcStr[8];
        FmtTime(timeStr, sizeof(timeStr), a->timestamp);
        snprintf(bfStr, sizeof(bfStr), "%lu%%", a->pageFileBefore);
        snprintf(afStr, sizeof(afStr), "%lu%%", a->pageFileAfter);
        snprintf(tcStr, sizeof(tcStr), "%d", a->trimmedCount);
        snprintf(fcStr, sizeof(fcStr), "%d", a->failedCount);

        LVITEMA item;
        memset(&item, 0, sizeof(item));
        item.mask = LVIF_TEXT; item.iItem = i; item.pszText = timeStr;
        LvInsertItem(g_hActionList, &item);
        LvSetItemText(g_hActionList, i, 1, bfStr);
        LvSetItemText(g_hActionList, i, 2, afStr);
        LvSetItemText(g_hActionList, i, 3, tcStr);
        LvSetItemText(g_hActionList, i, 4, fcStr);
        LvSetItemText(g_hActionList, i, 5, a->description);
    }
    LeaveCriticalSection(&g_csData);
}

static void RefreshDesktopData(HWND hwnd) {
    (void)hwnd;
    UpdateProcessList(hwnd);
    UpdateAnomalyList(hwnd);
    UpdateSuspiciousList(hwnd);
    UpdateActionList(hwnd);
    if (g_hChartArea) InvalidateRect(g_hChartArea, NULL, TRUE);
}

/* ============================================================================
 * Tab management
 * ============================================================================ */
typedef enum { TAB_OVERVIEW = 0, TAB_PROCESSES, TAB_CHART, TAB_ANOMALIES, TAB_SUSPICIOUS } TabIndex;

static void ShowTab(TabIndex tab) {
    int showOverview = (tab == TAB_OVERVIEW);
    int showProcs    = (tab == TAB_PROCESSES);
    int showChart    = (tab == TAB_CHART);
    int showAnomaly  = (tab == TAB_ANOMALIES);
    int showSusp     = (tab == TAB_SUSPICIOUS);

    ShowWindow(g_hProcList,    (showOverview || showProcs) ? SW_SHOW : SW_HIDE);
    ShowWindow(g_hActionList,  showOverview ? SW_SHOW : SW_HIDE);
    ShowWindow(g_hChartArea,   showChart ? SW_SHOW : SW_HIDE);
    ShowWindow(g_hComboRange,  showChart ? SW_SHOW : SW_HIDE);
    ShowWindow(g_hAnomalyList, showAnomaly ? SW_SHOW : SW_HIDE);
    ShowWindow(g_hSuspList,    showSusp ? SW_SHOW : SW_HIDE);
    if (showChart) InvalidateRect(g_hChartArea, NULL, TRUE);
}

/* ============================================================================
 * ListView creation helper
 * ============================================================================ */
static HWND CreateListView(HWND parent, int x, int y, int w, int h,
                            const char **headers, int nCols, int *colWidths) {
    HWND lv = CreateWindowExA(0, WC_LISTVIEWA, NULL,
        WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL,
        x, y, w, h, parent, NULL, GetModuleHandleA(NULL), NULL);
    LvSetExStyle(lv, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);

    LV_COLUMNA col;
    int i;
    for (i = 0; i < nCols; i++) {
        memset(&col, 0, sizeof(col));
        col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
        col.fmt = LVCFMT_LEFT;
        col.pszText = (LPSTR)headers[i];
        col.cx = colWidths[i];
        LvInsertColumn(lv, i, &col);
    }
    return lv;
}

/* ============================================================================
 * Status bar
 * ============================================================================ */
static void UpdateStatusBar(HWND hwnd) {
    (void)hwnd;
    if (!g_hStatusBar) return;
    char text[512];
    DWORD pf = g_latestSnapshot.pageFilePct;
    DWORD ph = g_latestSnapshot.physLoad;
    DWORD idle = g_latestSnapshot.idleSeconds;
    snprintf(text, sizeof(text),
        "  Page File: %lu%%  |  Physical: %lu%%  |  Idle: %lus  |  Port: %d  |  Uptime: ",
        pf, ph, idle, g_httpPort);
    int baseLen = (int)strlen(text);
    FmtDuration(text + baseLen, sizeof(text) - baseLen, time(NULL) - g_tStartTime);
    SendMessageA(g_hStatusBar, SB_SETTEXTA, 0, (LPARAM)text);
}

/* ============================================================================
 * Main window procedure
 * ============================================================================ */
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        InitCommonControls();
        g_hDesktopWnd = hwnd;

        g_hGuiFont = CreateFontA(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, FF_DONTCARE, "Segoe UI");

        RECT rc; GetClientRect(hwnd, &rc);
        int cw = rc.right - rc.left, ch = rc.bottom - rc.top;

        /* Tab control */
        g_hTab = CreateWindowExA(0, WC_TABCONTROLA, NULL,
            WS_CHILD | WS_VISIBLE | TCS_FIXEDWIDTH,
            4, 4, cw - 8, 26, hwnd, (HMENU)IDC_TAB,
            GetModuleHandleA(NULL), NULL);
        SendMessageA(g_hTab, WM_SETFONT, (WPARAM)g_hGuiFont, TRUE);

        TCITEMA tci;
        memset(&tci, 0, sizeof(tci)); tci.mask = TCIF_TEXT;
        tci.pszText = "Overview";    TcInsertItem(g_hTab, TAB_OVERVIEW, &tci);
        tci.pszText = "Processes";   TcInsertItem(g_hTab, TAB_PROCESSES, &tci);
        tci.pszText = "Charts";      TcInsertItem(g_hTab, TAB_CHART, &tci);
        tci.pszText = "Anomalies";   TcInsertItem(g_hTab, TAB_ANOMALIES, &tci);
        tci.pszText = "Suspicious";  TcInsertItem(g_hTab, TAB_SUSPICIOUS, &tci);

        /* Buttons */
        g_hBtnCleanup = CreateWindowExA(0, "BUTTON", "Cleanup Now",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            cw - 340, 4, 100, 26, hwnd, (HMENU)IDC_BTN_CLEANUP,
            GetModuleHandleA(NULL), NULL);
        SendMessageA(g_hBtnCleanup, WM_SETFONT, (WPARAM)g_hGuiFont, TRUE);

        CreateWindowExA(0, "BUTTON", "Exit Program",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            cw - 120, 4, 100, 26, hwnd, (HMENU)IDC_BTN_EXIT,
            GetModuleHandleA(NULL), NULL);
        SendMessageA(GetDlgItem(hwnd, IDC_BTN_EXIT), WM_SETFONT, (WPARAM)g_hGuiFont, TRUE);

        /* Chart range combo */
        g_hComboRange = CreateWindowExA(0, "COMBOBOX", NULL,
            WS_CHILD | CBS_DROPDOWNLIST | WS_VSCROLL,
            16, 32, 130, 200, hwnd, (HMENU)IDC_COMBO_RANGE,
            GetModuleHandleA(NULL), NULL);
        SendMessageA(g_hComboRange, WM_SETFONT, (WPARAM)g_hGuiFont, TRUE);
        SendMessageA(g_hComboRange, CB_ADDSTRING, 0, (LPARAM)"Past 24 Hours");
        SendMessageA(g_hComboRange, CB_ADDSTRING, 0, (LPARAM)"Past Week");
        SendMessageA(g_hComboRange, CB_ADDSTRING, 0, (LPARAM)"Past Month");
        SendMessageA(g_hComboRange, CB_ADDSTRING, 0, (LPARAM)"Past Year");
        SendMessageA(g_hComboRange, CB_SETCURSEL, CHART_WEEK, 0);
        ShowWindow(g_hComboRange, SW_HIDE);

        int listTop = 36, listH = ch - listTop - 24;

        /* Process list */
        {
            const char *headers[] = {"#", "PID", "Name", "Commit", "WS", "Growth"};
            int widths[] = {40, 70, 180, 110, 110, 100};
            g_hProcList = CreateListView(hwnd, 8, listTop, cw - 20, ch - listTop - 240, headers, 6, widths);
        }

        /* Action log */
        {
            const char *headers[] = {"Time", "Before", "After", "Trimmed", "Failed", "Description"};
            int widths[] = {130, 60, 60, 70, 60, 200};
            g_hActionList = CreateListView(hwnd, 8, ch - 230, cw - 20, 200, headers, 6, widths);
        }

        /* Chart area (custom window) */
        {
            WNDCLASSA chartCls;
            memset(&chartCls, 0, sizeof(chartCls));
            chartCls.lpfnWndProc   = ChartProc;
            chartCls.hInstance     = GetModuleHandleA(NULL);
            chartCls.hCursor       = LoadCursor(NULL, IDC_ARROW);
            chartCls.hbrBackground = CreateSolidBrush(CLR_CARD);
            chartCls.lpszClassName = "VMChartArea";
            RegisterClassA(&chartCls);

            g_hChartArea = CreateWindowExA(0, "VMChartArea", NULL,
                WS_CHILD | WS_VISIBLE | WS_BORDER,
                8, 60, cw - 20, ch - 90, hwnd, (HMENU)IDC_CHART_AREA,
                GetModuleHandleA(NULL), NULL);
        }

        /* Anomaly list */
        {
            const char *headers[] = {"Time", "Type", "PID", "Process", "Value", "Description"};
            int widths[] = {130, 90, 60, 140, 70, 300};
            g_hAnomalyList = CreateListView(hwnd, 8, listTop, cw - 20, listH, headers, 6, widths);
        }

        /* Suspicious list */
        {
            const char *headers[] = {"PID", "Name", "First", "Last", "Growth", "Peak Rate", "First Seen", "Last Seen", "Alerts"};
            int widths[] = {55, 130, 100, 100, 100, 90, 130, 130, 50};
            g_hSuspList = CreateListView(hwnd, 8, listTop, cw - 20, listH, headers, 9, widths);
        }

        /* Status bar */
        g_hStatusBar = CreateWindowExA(0, STATUSCLASSNAMEA, NULL,
            WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
            0, 0, 0, 0, hwnd, NULL, GetModuleHandleA(NULL), NULL);
        SendMessageA(g_hStatusBar, WM_SETFONT, (WPARAM)g_hGuiFont, TRUE);

        ShowTab(TAB_OVERVIEW);
        AddTrayIcon(hwnd);
        SetTimer(hwnd, IDT_DESKTOP_REFRESH, DESKTOP_REFRESH_MS, NULL);
        return 0;
    }

    case WM_NOTIFY: {
        NMHDR *nmhdr = (NMHDR *)lParam;
        if (nmhdr->idFrom == IDC_TAB && nmhdr->code == TCN_SELCHANGE) {
            int sel = TcGetCurSel(g_hTab);
            switch (sel) {
            case 0: ShowTab(TAB_OVERVIEW);    break;
            case 1: ShowTab(TAB_PROCESSES);   break;
            case 2: ShowTab(TAB_CHART);       break;
            case 3: ShowTab(TAB_ANOMALIES);   break;
            case 4: ShowTab(TAB_SUSPICIOUS);  break;
            }
            RefreshDesktopData(hwnd);
        }
        return 0;
    }

    case WM_COMMAND: {
        WORD id = LOWORD(wParam);
        switch (id) {
        case IDC_BTN_CLEANUP:
            CheckAndAct();
            RefreshDesktopData(hwnd);
            break;
        case IDC_BTN_EXIT:
            DestroyWindow(hwnd);
            break;
        case IDC_COMBO_RANGE:
            if (HIWORD(wParam) == CBN_SELCHANGE) {
                g_chartRange = (int)SendMessageA((HWND)lParam, CB_GETCURSEL, 0, 0);
                InvalidateRect(g_hChartArea, NULL, TRUE);
            }
            break;
        case IDM_SHOW:
            ShowWindow(hwnd, SW_RESTORE);
            SetForegroundWindow(hwnd);
            break;
        case IDM_CLEANUP:
            CheckAndAct();
            RefreshDesktopData(hwnd);
            break;
        case IDM_EXIT:
            DestroyWindow(hwnd);
            break;
        }
        return 0;
    }

    case WM_TRAYICON:
        if (LOWORD(lParam) == WM_RBUTTONUP) ShowTrayMenu(hwnd);
        else if (LOWORD(lParam) == WM_LBUTTONDBLCLK) {
            ShowWindow(hwnd, SW_RESTORE);
            SetForegroundWindow(hwnd);
        }
        return 0;

    case WM_SYSCOMMAND:
        if ((wParam & 0xFFF0) == SC_MINIMIZE) {
            ShowWindow(hwnd, SW_HIDE);
            return 0;
        }
        break;

    case WM_CLOSE:
        ShowWindow(hwnd, SW_HIDE);
        return 0;

    case WM_DESTROY:
        KillTimer(hwnd, IDT_DESKTOP_REFRESH);
        RemoveTrayIcon(hwnd);
        PostQuitMessage(0);
        return 0;

    case WM_TIMER:
        if (wParam == IDT_DESKTOP_REFRESH) {
            RefreshDesktopData(hwnd);
            UpdateStatusBar(hwnd);
        }
        return 0;

    case WM_SIZE: {
        RECT rc; GetClientRect(hwnd, &rc);
        int cw = rc.right - rc.left, ch = rc.bottom - rc.top;
        if (g_hTab)       SetWindowPos(g_hTab, NULL, 4, 4, cw - 8, 26, SWP_NOZORDER);
        if (g_hBtnCleanup)SetWindowPos(g_hBtnCleanup, NULL, cw - 340, 4, 100, 26, SWP_NOZORDER);
        {
            HWND btn = GetDlgItem(hwnd, IDC_BTN_EXIT);
            if (btn) SetWindowPos(btn, NULL, cw - 120, 4, 100, 26, SWP_NOZORDER);
        }
        int listTop = 36, listH = ch - listTop - 24;
        if (g_hProcList)   SetWindowPos(g_hProcList, NULL, 8, listTop, cw - 20, ch - listTop - 240, SWP_NOZORDER);
        if (g_hActionList) SetWindowPos(g_hActionList, NULL, 8, ch - 230, cw - 20, 200, SWP_NOZORDER);
        if (g_hChartArea)  SetWindowPos(g_hChartArea, NULL, 8, 60, cw - 20, ch - 90, SWP_NOZORDER);
        if (g_hAnomalyList)SetWindowPos(g_hAnomalyList, NULL, 8, listTop, cw - 20, listH, SWP_NOZORDER);
        if (g_hSuspList)   SetWindowPos(g_hSuspList, NULL, 8, listTop, cw - 20, listH, SWP_NOZORDER);
        SendMessageA(g_hStatusBar, WM_SIZE, 0, 0);
        return 0;
    }
    }

    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

/* ============================================================================
 * RunDesktop
 * ============================================================================ */
int RunDesktop(void) {
    g_bDesktop = TRUE;

    WNDCLASSA wc;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = GetModuleHandleA(NULL);
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(CLR_BG);
    wc.lpszClassName = "VMManagerDesktop";
    wc.hIcon         = LoadIcon(NULL, IDI_INFORMATION);
    if (!RegisterClassA(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return 1;

    HWND hwnd = CreateWindowExA(0, "VMManagerDesktop",
        "VM Manager v4.0 — Memory Monitor & Suspicious Process Detector",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, 1024, 720,
        NULL, NULL, GetModuleHandleA(NULL), NULL);
    if (!hwnd) return 1;

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    /* Start engine if not already running */
    if (!g_hHttpThread) {
        g_hHttpThread = CreateThread(NULL, 0, HttpServerThread, NULL, 0, NULL);
        int wc2 = 0;
        while (g_httpPort == 0 && wc2 < 30) { Sleep(100); wc2++; }
    }

    /* Initial data collection */
    CheckAndAct();

    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    g_bRunning = FALSE;
    RemoveTrayIcon(hwnd);
    return 0;
}
