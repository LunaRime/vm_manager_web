/**
 * vm_desktop.cpp — C++ Desktop GUI implementation
 *
 * Dark-themed Win32 window with 6 tabs, GDI charts, system tray,
 * encrypted database browser, and full i18n support (zh-CN/zh-TW/en-US).
 *
 * Uses _R() helper instead of C99 compound literals for C++ compatibility.
 */
#include "vm_desktop.hpp"
#include <commdlg.h>

// ---- C++-compatible RECT literal (replaces &(RECT){...}) ----
static RECT& _R(int l, int t, int r, int b) {
    static RECT bufs[8]; static int n = 0;
    RECT& rc = bufs[n]; n = (n+1) % 8;
    rc.left = l; rc.top = t; rc.right = r; rc.bottom = b; return rc;
}

// ============================================================================
// Static constants
// ============================================================================
const COLORREF VMDesktopApp::kBg       = RGB(13, 17, 23);
const COLORREF VMDesktopApp::kCard     = RGB(22, 27, 34);
const COLORREF VMDesktopApp::kCard2    = RGB(30, 35, 42);
const COLORREF VMDesktopApp::kBorder   = RGB(48, 54, 61);
const COLORREF VMDesktopApp::kBorderLt = RGB(58, 64, 71);
const COLORREF VMDesktopApp::kText     = RGB(225, 230, 237);
const COLORREF VMDesktopApp::kText2    = RGB(201, 209, 217);
const COLORREF VMDesktopApp::kMuted    = RGB(139, 148, 158);
const COLORREF VMDesktopApp::kAccent   = RGB(88, 166, 255);
const COLORREF VMDesktopApp::kGreen    = RGB(63, 185, 80);
const COLORREF VMDesktopApp::kRed      = RGB(248, 81, 73);
const COLORREF VMDesktopApp::kOrange   = RGB(255, 159, 50);
const COLORREF VMDesktopApp::kYellow   = RGB(210, 153, 34);
const COLORREF VMDesktopApp::kPfColor  = RGB(88, 166, 255);
const COLORREF VMDesktopApp::kPhColor  = RGB(63, 185, 80);
const WCHAR *VMDesktopApp::kMainClass     = L"VMMgrDesktopV5W";
const WCHAR *VMDesktopApp::kChartClass    = L"VMChartPanelV5W";
const WCHAR *VMDesktopApp::kOverviewClass = L"VMOverviewPaneV5W";

// ============================================================================
// Constructor / Destructor
// ============================================================================
VMDesktopApp::VMDesktopApp()
    : m_hInst(nullptr), m_hWnd(nullptr)
    , m_hFont(nullptr), m_hTitleFont(nullptr), m_hMonoFont(nullptr)
    , m_hTab(nullptr), m_hBtnCleanup(nullptr), m_hBtnExport(nullptr), m_hBtnExit(nullptr)
    , m_hOverviewPane(nullptr), m_hProcList(nullptr), m_hDbPanel(nullptr)
    , m_hChartArea(nullptr), m_hAnomList(nullptr), m_hSuspList(nullptr)
    , m_hActionLog(nullptr)
    , m_hCboRange(nullptr), m_hLblRange(nullptr), m_hBtnChartMode(nullptr)
    , m_hStatus(nullptr)
    , m_activeTab(TAB_OV), m_chartRange(CHART_WEEK), m_chartMode(0), m_running(false) {}

VMDesktopApp::~VMDesktopApp() {
    if (m_hFont) DeleteObject(m_hFont);
    if (m_hTitleFont) DeleteObject(m_hTitleFont);
    if (m_hMonoFont) DeleteObject(m_hMonoFont);
}

// ============================================================================
// Public entry point
// ============================================================================
int VMDesktopApp::Run(HINSTANCE hInst, int nCmdShow) {
    m_hInst = hInst;
    if (!RegisterWindowClass()) return 1;
    if (!CreateMainWindow(nCmdShow)) return 1;
    g_bDesktop = TRUE; m_running = true;
    MSG msg;
    while (m_running && GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg); DispatchMessageW(&msg);
    }
    g_bRunning = FALSE;
    return 0;
}

bool VMDesktopApp::RegisterWindowClass() {
    WNDCLASSW wc = {};
    wc.lpfnWndProc = WndProc; wc.hInstance = m_hInst;
    wc.hCursor = LoadCursorA(nullptr, (LPCSTR)IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(kBg);
    wc.lpszClassName = kMainClass;
    wc.hIcon = LoadIconA(nullptr, (LPCSTR)IDI_APPLICATION);
    if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
    wc.lpfnWndProc = ChartWndProc; wc.hbrBackground = CreateSolidBrush(kCard);
    wc.lpszClassName = kChartClass;
    if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
    wc.lpfnWndProc = OverviewWndProc; wc.hbrBackground = CreateSolidBrush(kBg);
    wc.lpszClassName = kOverviewClass;
    if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
    return true;
}

bool VMDesktopApp::CreateMainWindow(int nCmdShow) {
    VMI18n &i18n = VMI18n::Instance();
    m_hWnd = CreateWindowExW(0, kMainClass, i18n.GetW(i18n::APP_TITLE),
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, 1100, 740,
        nullptr, nullptr, m_hInst, this);
    if (!m_hWnd) return false;
    HMODULE hDwm = LoadLibraryA("dwmapi.dll");
    if (hDwm) {
        typedef HRESULT (WINAPI *Pfn)(HWND,DWORD,LPCVOID,DWORD);
        auto pfn = (Pfn)GetProcAddress(hDwm, "DwmSetWindowAttribute");
        if (pfn) { BOOL d=TRUE; pfn(m_hWnd, 20, &d, sizeof(d)); }
    }
    ShowWindow(m_hWnd, nCmdShow); UpdateWindow(m_hWnd);
    return true;
}

// ============================================================================
// Child controls
// ============================================================================
void VMDesktopApp::CreateChildControls() {
    VMI18n &i18n = VMI18n::Instance();
    RECT rc; GetClientRect(m_hWnd, &rc);
    int cw = rc.right, ch = rc.bottom;

    LocaleId loc = i18n.GetLocale();
    const char *face = (loc == LOC_ZH_TW) ? "Microsoft JhengHei" : "Microsoft YaHei";
    m_hFont = CreateFontA(14,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,
        DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,6,FF_DONTCARE,face);
    m_hTitleFont = CreateFontA(18,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,
        DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,6,FF_DONTCARE,face);
    m_hMonoFont = CreateFontA(13,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,
        DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,6,FF_DONTCARE,"Consolas");

    /* Tab */
    m_hTab = CreateWindowExW(0, WC_TABCONTROLW, nullptr,
        WS_CHILD|WS_VISIBLE|TCS_FIXEDWIDTH, 8,6,cw-16,28,m_hWnd,(HMENU)ID_TAB,m_hInst,nullptr);
    SendMessageW(m_hTab, WM_SETFONT, (WPARAM)m_hFont, TRUE);
    TCITEMA tci={}; tci.mask=TCIF_TEXT;
    const char *tabs[]={i18n::TAB_OVERVIEW,i18n::TAB_PROCESSES,i18n::TAB_DATABASE,
        i18n::TAB_CHARTS,i18n::TAB_ANOMALIES,i18n::TAB_SUSPICIOUS};
    for(int i=0;i<6;i++){tci.pszText=(LPSTR)i18n.Get(tabs[i]).c_str(); SendMessageA(m_hTab,TCM_INSERTITEMA,i,(LPARAM)&tci);}

    /* Buttons */
    m_hBtnExport = CreateWindowExW(0, L"BUTTON", i18n.GetW(i18n::BTN_EXPORT),
        WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON|BS_FLAT, cw-460,8,110,26, m_hWnd,(HMENU)ID_BTN_EXPORT,m_hInst,nullptr);
    SendMessageW(m_hBtnExport, WM_SETFONT, (WPARAM)m_hFont, TRUE);
    m_hBtnCleanup = CreateWindowExW(0, L"BUTTON", i18n.GetW(i18n::BTN_CLEANUP),
        WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON|BS_FLAT, cw-340,8,110,26, m_hWnd,(HMENU)ID_BTN_CLEANUP,m_hInst,nullptr);
    SendMessageW(m_hBtnCleanup, WM_SETFONT, (WPARAM)m_hFont, TRUE);
    m_hBtnExit = CreateWindowExW(0, L"BUTTON", i18n.GetW(i18n::BTN_EXIT),
        WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON|BS_FLAT, cw-120,8,100,26, m_hWnd,(HMENU)ID_BTN_EXIT,m_hInst,nullptr);
    SendMessageW(m_hBtnExit, WM_SETFONT, (WPARAM)m_hFont, TRUE);

    /* Chart controls (hidden) */
    m_hLblRange = CreateWindowExW(0, L"STATIC", i18n.GetW(i18n::CHART_RANGE_LBL),
        WS_CHILD|SS_LEFT,16,42,100,20,m_hWnd,nullptr,m_hInst,nullptr);
    SendMessageW(m_hLblRange, WM_SETFONT, (WPARAM)m_hFont, TRUE); ShowWindow(m_hLblRange, SW_HIDE);
    m_hCboRange = CreateWindowExW(0, L"COMBOBOX", nullptr,
        WS_CHILD|CBS_DROPDOWNLIST|WS_VSCROLL, 120,40,140,200,m_hWnd,(HMENU)ID_CBO_RANGE,m_hInst,nullptr);
    SendMessageW(m_hCboRange, WM_SETFONT, (WPARAM)m_hFont, TRUE);
    SendMessageW(m_hCboRange, CB_ADDSTRING, 0, (LPARAM)i18n.GetW(i18n::CHART_RANGE_DAY));
    SendMessageW(m_hCboRange, CB_ADDSTRING, 0, (LPARAM)i18n.GetW(i18n::CHART_RANGE_WEEK));
    SendMessageW(m_hCboRange, CB_ADDSTRING, 0, (LPARAM)i18n.GetW(i18n::CHART_RANGE_MONTH));
    SendMessageW(m_hCboRange, CB_ADDSTRING, 0, (LPARAM)i18n.GetW(i18n::CHART_RANGE_YEAR));
    SendMessageW(m_hCboRange, CB_SETCURSEL, 1, 0); ShowWindow(m_hCboRange, SW_HIDE);
    m_hBtnChartMode = CreateWindowExW(0, L"BUTTON", L"Line",
        WS_CHILD|BS_PUSHBUTTON|BS_FLAT, 270,40,60,22,m_hWnd,(HMENU)ID_BTN_CHART,m_hInst,nullptr);
    SendMessageW(m_hBtnChartMode, WM_SETFONT, (WPARAM)m_hFont, TRUE); ShowWindow(m_hBtnChartMode, SW_HIDE);

    /* Panels & Lists */
    m_hOverviewPane = CreateWindowExW(0, kOverviewClass, nullptr, WS_CHILD|WS_VISIBLE,
        8,44,cw-16,90, m_hWnd,(HMENU)ID_OV_PANE,m_hInst,nullptr);

    const char *pHdr[]={i18n::HDR_RANK,i18n::HDR_PID,i18n::HDR_NAME,i18n::HDR_COMMIT,i18n::HDR_WS,i18n::HDR_GROWTH};
    int pW[]={36,70,200,120,120,110};
    m_hProcList = CreateListView(8,142,cw-16,ch-190, pHdr,pW,6,ID_LIST_PROC);

    const char *aHdr[]={i18n::HDR_TIME,i18n::HDR_BEFORE,i18n::HDR_AFTER,i18n::HDR_TRIMMED,i18n::HDR_FAILED,i18n::HDR_DESC};
    int aW[]={130,60,60,60,50,290};
    m_hActionLog = CreateListView(8,ch-240,cw-16,200, aHdr,aW,6,ID_LIST_ACTS);

    m_hDbPanel = CreateWindowExW(0, L"STATIC", nullptr, WS_CHILD|SS_OWNERDRAW,
        8,44,cw-16,ch-64, m_hWnd,(HMENU)ID_DB_PANEL,m_hInst,nullptr);
    m_hChartArea = CreateWindowExW(0, kChartClass, nullptr, WS_CHILD|WS_BORDER,
        8,68,cw-16,ch-100, m_hWnd,(HMENU)ID_CHART_AREA,m_hInst,nullptr);

    const char *nHdr[]={i18n::HDR_TIME,i18n::HDR_TYPE,i18n::HDR_PID,i18n::HDR_PROCESS,i18n::HDR_VALUE,i18n::HDR_DESC};
    int nW[]={130,100,55,140,70,360};
    m_hAnomList = CreateListView(8,44,cw-16,ch-64, nHdr,nW,6,ID_LIST_ANOM);

    const char *sHdr[]={i18n::HDR_PID,i18n::HDR_NAME,i18n::HDR_FIRST,i18n::HDR_LAST,
        i18n::HDR_GROWTH,i18n::HDR_PEAK_RATE,i18n::HDR_FIRST_SEEN,i18n::HDR_LAST_SEEN,i18n::HDR_ALERTS};
    int sW[]={55,130,100,100,95,90,135,135,50};
    m_hSuspList = CreateListView(8,44,cw-16,ch-64, sHdr,sW,9,ID_LIST_SUSP);

    m_hStatus = CreateWindowExW(0, STATUSCLASSNAMEW, nullptr, WS_CHILD|WS_VISIBLE|SBARS_SIZEGRIP,
        0,0,0,0, m_hWnd,nullptr,m_hInst,nullptr);
    SendMessageW(m_hStatus, WM_SETFONT, (WPARAM)m_hFont, TRUE);

    ShowTab(TAB_OV); AddTrayIcon();
    SetTimer(m_hWnd, kRefreshTimer, kRefreshMs, nullptr);
}

// ============================================================================
// ListView helpers
// ============================================================================
HWND VMDesktopApp::CreateListView(int x,int y,int w,int h,const char **hdrs,int *wds,int n,int id) {
    HWND lv = CreateWindowExW(0, WC_LISTVIEWW, nullptr,
        WS_CHILD|WS_VISIBLE|LVS_REPORT|LVS_SINGLESEL|LVS_SHOWSELALWAYS,
        x,y,w,h, m_hWnd,(HMENU)id,m_hInst,nullptr);
    SendMessageA(lv, LVM_SETEXTENDEDLISTVIEWSTYLE, 0, LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER|LVS_EX_GRIDLINES);
    VMI18n &i18n = VMI18n::Instance();
    for(int i=0;i<n;i++){LV_COLUMNA c={};c.mask=LVCF_TEXT|LVCF_WIDTH|LVCF_FMT;c.fmt=LVCFMT_LEFT;
        c.pszText=(LPSTR)i18n.Get(hdrs[i]).c_str();c.cx=wds[i];SendMessageA(lv,LVM_INSERTCOLUMNA,i,(LPARAM)&c);}
    ListView_SetBkColor(lv,kCard);ListView_SetTextBkColor(lv,kCard);ListView_SetTextColor(lv,kText2);
    return lv;
}
void VMDesktopApp::AddLVItem(HWND lv,int row,int col,const char *txt) {
    LVITEMA it={};it.iSubItem=col;it.pszText=(LPSTR)txt;
    if(col==0){it.mask=LVIF_TEXT;it.iItem=row;SendMessageA(lv,LVM_INSERTITEMA,0,(LPARAM)&it);}
    else SendMessageA(lv,LVM_SETITEMTEXTA,row,(LPARAM)&it);
}
void VMDesktopApp::ClearLV(HWND lv){SendMessageA(lv,LVM_DELETEALLITEMS,0,0);}

// ============================================================================
// Tabs & Layout
// ============================================================================
void VMDesktopApp::ShowTab(Tab t) {
    m_activeTab=t; int ov=t==TAB_OV,pr=t==TAB_PROC,db=t==TAB_DB,ch=t==TAB_CHART,an=t==TAB_ANOM,su=t==TAB_SUSP;
    ShowWindow(m_hOverviewPane,ov?SW_SHOW:SW_HIDE); ShowWindow(m_hProcList,(ov||pr)?SW_SHOW:SW_HIDE);
    ShowWindow(m_hActionLog,ov?SW_SHOW:SW_HIDE); ShowWindow(m_hDbPanel,db?SW_SHOW:SW_HIDE);
    ShowWindow(m_hChartArea,ch?SW_SHOW:SW_HIDE); ShowWindow(m_hCboRange,ch?SW_SHOW:SW_HIDE);
    ShowWindow(m_hLblRange,ch?SW_SHOW:SW_HIDE); ShowWindow(m_hBtnChartMode,ch?SW_SHOW:SW_HIDE);
    ShowWindow(m_hAnomList,an?SW_SHOW:SW_HIDE); ShowWindow(m_hSuspList,su?SW_SHOW:SW_HIDE);
}
void VMDesktopApp::LayoutChildren(int cw,int ch) {
    int ct=44,cl=8,cr=cw-16;
    if(m_hTab)SetWindowPos(m_hTab,nullptr,8,6,cw-16,28,SWP_NOZORDER);
    if(m_hBtnExport)SetWindowPos(m_hBtnExport,nullptr,cw-460,8,110,26,SWP_NOZORDER);
    if(m_hBtnCleanup)SetWindowPos(m_hBtnCleanup,nullptr,cw-340,8,110,26,SWP_NOZORDER);
    if(m_hBtnExit)SetWindowPos(m_hBtnExit,nullptr,cw-120,8,100,26,SWP_NOZORDER);
    if(m_hOverviewPane)SetWindowPos(m_hOverviewPane,nullptr,cl,ct,cr,90,SWP_NOZORDER);
    if(m_hProcList)SetWindowPos(m_hProcList,nullptr,cl,ct+98,cr,ch-ct-148,SWP_NOZORDER);
    if(m_hActionLog)SetWindowPos(m_hActionLog,nullptr,cl,ch-240,cr,200,SWP_NOZORDER);
    if(m_hDbPanel)SetWindowPos(m_hDbPanel,nullptr,cl,ct,cr,ch-ct-20,SWP_NOZORDER);
    if(m_hChartArea)SetWindowPos(m_hChartArea,nullptr,cl,68,cr,ch-100,SWP_NOZORDER);
    if(m_hAnomList)SetWindowPos(m_hAnomList,nullptr,cl,ct,cr,ch-ct-20,SWP_NOZORDER);
    if(m_hSuspList)SetWindowPos(m_hSuspList,nullptr,cl,ct,cr,ch-ct-20,SWP_NOZORDER);
    SendMessageW(m_hStatus,WM_SIZE,0,0);
}

// ============================================================================
// GDI Line Chart
// ============================================================================
void VMDesktopApp::DrawLineChart(HDC hdc, RECT rc) {
    int w=rc.right-rc.left, h=rc.bottom-rc.top; if(w<=0||h<=0)return;
    HBRUSH hBg=CreateSolidBrush(kCard); FillRect(hdc,&rc,hBg); DeleteObject(hBg);
    int pt=36,pb=44,pl=60,pr=36, pw=w-pl-pr,ph=h-pt-pb; if(pw<=0||ph<=0)return;

    AggBucket *buckets=nullptr; int count=0,maxTake=30;
    EnterCriticalSection(&g_csData);
    switch(m_chartRange){case CHART_DAY:buckets=g_hourlyBuckets;count=g_hourlyCount;maxTake=24;break;
        case CHART_WEEK:buckets=g_dailyBuckets;count=g_dailyCount;maxTake=7;break;
        case CHART_MONTH:buckets=g_dailyBuckets;count=g_dailyCount;maxTake=30;break;
        case CHART_YEAR:buckets=g_monthlyBuckets;count=g_monthlyCount;maxTake=12;break;}
    int take=count<maxTake?count:maxTake, start=count-take; if(start<0){start=0;take=count;}
    AggBucket *plot=(AggBucket*)HeapAlloc(GetProcessHeap(),0,take*sizeof(AggBucket));
    if(plot)memcpy(plot,buckets+start,take*sizeof(AggBucket));
    LeaveCriticalSection(&g_csData);
    if(!plot||take<2){if(plot)HeapFree(GetProcessHeap(),0,plot);
        SetTextColor(hdc,kMuted);SetBkMode(hdc,TRANSPARENT);
        DrawTextA(hdc,(char*)VMI18n::Instance().Get(i18n::DB_NO_DATA).c_str(),-1,&rc,DT_CENTER|DT_VCENTER|DT_SINGLELINE);return;}

    HPEN hThr=CreatePen(PS_DASH,1,kOrange),hOld=(HPEN)SelectObject(hdc,hThr);
    int thrY=pt+ph-(ph*85/100); MoveToEx(hdc,pl,thrY,nullptr);LineTo(hdc,pl+pw,thrY);
    SelectObject(hdc,hOld);DeleteObject(hThr);

    HPEN hGd=CreatePen(PS_SOLID,1,kBorder); hOld=(HPEN)SelectObject(hdc,hGd);
    for(int g=0;g<=100;g+=25){int y=pt+ph-(ph*g/100);MoveToEx(hdc,pl,y,nullptr);LineTo(hdc,pl+pw,y);
        char lb[8];snprintf(lb,sizeof(lb),"%d%%",g);SetTextColor(hdc,kMuted);
        DrawTextA(hdc,lb,-1,&_R(2,y-8,pl-4,y+8),DT_RIGHT|DT_VCENTER|DT_SINGLELINE);}
    SelectObject(hdc,hOld);DeleteObject(hGd);

    HPEN hPf=CreatePen(PS_SOLID,3,kPfColor);hOld=(HPEN)SelectObject(hdc,hPf);
    for(int i=0;i<take;i++){double v=plot[i].sampleCount>0?plot[i].pfSum/plot[i].sampleCount:0;
        int x=pl+(int)((double)i/(take-1)*pw),y=pt+ph-(int)(v/100.0*ph);
        if(y<pt)y=pt;if(y>pt+ph)y=pt+ph; i==0?MoveToEx(hdc,x,y,nullptr):LineTo(hdc,x,y);}
    SelectObject(hdc,hOld);DeleteObject(hPf);

    HPEN hPh=CreatePen(PS_DASH,2,kPhColor);hOld=(HPEN)SelectObject(hdc,hPh);
    for(int i=0;i<take;i++){double v=plot[i].sampleCount>0?plot[i].phSum/plot[i].sampleCount:0;
        int x=pl+(int)((double)i/(take-1)*pw),y=pt+ph-(int)(v/100.0*ph);
        if(y<pt)y=pt;if(y>pt+ph)y=pt+ph; i==0?MoveToEx(hdc,x,y,nullptr):LineTo(hdc,x,y);}
    SelectObject(hdc,hOld);DeleteObject(hPh);

    int dotStep=take>12?take/6:1;
    for(int i=0;i<take;i+=dotStep){double v=plot[i].sampleCount>0?plot[i].pfMax:0;
        int x=pl+(int)((double)i/(take-1)*pw),y=pt+ph-(int)(v/100.0*ph);
        HBRUSH hD=CreateSolidBrush(kPfColor);HPEN hDP=CreatePen(PS_SOLID,2,kCard);
        SelectObject(hdc,hD);SelectObject(hdc,hDP);Ellipse(hdc,x-4,y-4,x+4,y+4);
        SelectObject(hdc,hOld);DeleteObject(hD);DeleteObject(hDP);
        char mx[8];snprintf(mx,sizeof(mx),"%.0f%%",v);SetTextColor(hdc,kPfColor);
        DrawTextA(hdc,mx,-1,&_R(x-18,y-22,x+18,y-8),DT_CENTER|DT_BOTTOM|DT_SINGLELINE);}

    /* Legend */
    {RECT lg={pl,8,pl+200,26};HBRUSH hLg=CreateSolidBrush(kCard2);HPEN hLgP=CreatePen(PS_SOLID,1,kBorderLt);
        SelectObject(hdc,hLg);SelectObject(hdc,hLgP);RoundRect(hdc,lg.left,lg.top,lg.right,lg.bottom,8,8);
        DeleteObject(hLg);DeleteObject(hLgP);
        VMI18n &i18n=VMI18n::Instance();
        HBRUSH b1=CreateSolidBrush(kPfColor);FillRect(hdc,&_R(pl+8,13,pl+20,21),b1);DeleteObject(b1);
        SetTextColor(hdc,kText);DrawTextA(hdc,(char*)i18n.Get(i18n::CHART_LEGEND_PF).c_str(),-1,
            &_R(pl+24,11,pl+80,23),DT_LEFT|DT_VCENTER|DT_SINGLELINE);
        HBRUSH b2=CreateSolidBrush(kPhColor);FillRect(hdc,&_R(pl+88,13,pl+100,21),b2);DeleteObject(b2);
        DrawTextA(hdc,(char*)i18n.Get(i18n::CHART_LEGEND_PH).c_str(),-1,
            &_R(pl+104,11,pl+170,23),DT_LEFT|DT_VCENTER|DT_SINGLELINE);
        SelectObject(hdc,hOld);}
    HeapFree(GetProcessHeap(),0,plot);
}

// ============================================================================
// GDI Bar Chart
// ============================================================================
void VMDesktopApp::DrawBarChart(HDC hdc, RECT rc) {
    int w=rc.right-rc.left,h=rc.bottom-rc.top;if(w<=0||h<=0)return;
    HBRUSH hBg=CreateSolidBrush(kCard);FillRect(hdc,&rc,hBg);DeleteObject(hBg);
    int pt=36,pb=44,pl=60,pr=36,pw=w-pl-pr,ph=h-pt-pb;if(pw<=0||ph<=0)return;

    int maxTake=12;AggBucket*buckets=nullptr;int count=0;
    EnterCriticalSection(&g_csData);
    switch(m_chartRange){case CHART_DAY:buckets=g_hourlyBuckets;count=g_hourlyCount;maxTake=12;break;
        case CHART_WEEK:buckets=g_dailyBuckets;count=g_dailyCount;maxTake=7;break;
        case CHART_MONTH:buckets=g_dailyBuckets;count=g_dailyCount;maxTake=12;break;
        case CHART_YEAR:buckets=g_monthlyBuckets;count=g_monthlyCount;maxTake=12;break;}
    int take=count<maxTake?count:maxTake,start=count-take;if(start<0){start=0;take=count;}
    AggBucket*plot=(AggBucket*)HeapAlloc(GetProcessHeap(),0,take*sizeof(AggBucket));
    if(plot)memcpy(plot,buckets+start,take*sizeof(AggBucket));
    LeaveCriticalSection(&g_csData); if(!plot||take<1){if(plot)HeapFree(GetProcessHeap(),0,plot);return;}

    HPEN hThr=CreatePen(PS_DASH,1,kOrange),hOld=(HPEN)SelectObject(hdc,hThr);
    int thrY=pt+ph-(ph*85/100);MoveToEx(hdc,pl,thrY,nullptr);LineTo(hdc,pl+pw,thrY);
    SelectObject(hdc,hOld);DeleteObject(hThr);

    HPEN hGd=CreatePen(PS_SOLID,1,kBorder);hOld=(HPEN)SelectObject(hdc,hGd);
    for(int g=0;g<=100;g+=25){int y=pt+ph-(ph*g/100);MoveToEx(hdc,pl,y,nullptr);LineTo(hdc,pl+pw,y);
        char lb[8];snprintf(lb,sizeof(lb),"%d%%",g);SetTextColor(hdc,kMuted);
        DrawTextA(hdc,lb,-1,&_R(2,y-8,pl-4,y+8),DT_RIGHT|DT_VCENTER|DT_SINGLELINE);}
    SelectObject(hdc,hOld);DeleteObject(hGd);

    int nGroups=take,barsPerGroup=2,groupGap=12,avail=pw-groupGap*(nGroups-1);
    int barW=avail/(nGroups*barsPerGroup);if(barW<4)barW=4;
    for(int i=0;i<take;i++){double pf=plot[i].sampleCount>0?plot[i].pfSum/plot[i].sampleCount:0;
        double phv=plot[i].sampleCount>0?plot[i].phSum/plot[i].sampleCount:0;
        int gx=pl+(int)((double)i/(take-1)*pw),bx=gx-(barsPerGroup*barW+groupGap)/2;
        int pfH=(int)(pf/100.0*ph);if(pfH<2)pfH=2;HBRUSH bPf=CreateSolidBrush(kPfColor);
        FillRect(hdc,&_R(bx,pt+ph-pfH,bx+barW,pt+ph),bPf);DeleteObject(bPf);
        int phH=(int)(phv/100.0*ph);if(phH<2)phH=2;HBRUSH bPh=CreateSolidBrush(kPhColor);
        FillRect(hdc,&_R(bx+barW+2,pt+ph-phH,bx+barW*2+2,pt+ph),bPh);DeleteObject(bPh);}

    SetTextColor(hdc,kMuted);
    for(int i=0;i<take;i++){if(take>8&&i%(take/4)!=0&&i!=take-1)continue;
        int x=pl+(int)((double)i/(take-1)*pw);char lb[32];struct tm*tm=localtime(&plot[i].bucketStart);
        if(m_chartRange==CHART_DAY)strftime(lb,sizeof(lb),"%H:%M",tm);
        else if(m_chartRange==CHART_YEAR)strftime(lb,sizeof(lb),"%b",tm);
        else strftime(lb,sizeof(lb),"%m/%d",tm);
        DrawTextA(hdc,lb,-1,&_R(x-25,pt+ph+6,x+25,pt+ph+22),DT_CENTER|DT_TOP|DT_SINGLELINE);}
    HeapFree(GetProcessHeap(),0,plot);
}

// ============================================================================
// Overview cards
// ============================================================================
void VMDesktopApp::DrawOverviewCards(HDC hdc, RECT rc) {
    VMI18n &i18n=VMI18n::Instance(); int cw=rc.right-rc.left,ch=90;
    HBRUSH hBg=CreateSolidBrush(kCard);HPEN hBdr=CreatePen(PS_SOLID,1,kBorder);
    SelectObject(hdc,hBg);SelectObject(hdc,hBdr);
    Rectangle(hdc,rc.left,rc.top,rc.right,rc.top+ch);DeleteObject(hBg);DeleteObject(hBdr);

    SetTextColor(hdc,kText);SetBkMode(hdc,TRANSPARENT);
    if(m_hTitleFont)SelectObject(hdc,m_hTitleFont);
    DrawTextA(hdc,(char*)i18n.Get(i18n::CARD_TITLE).c_str(),-1,
        &_R(rc.left+12,rc.top+8,rc.right-12,rc.top+30),DT_LEFT|DT_VCENTER|DT_SINGLELINE);

    EnterCriticalSection(&g_csData);
    DWORD pf=g_latestSnapshot.pageFilePct,ph=g_latestSnapshot.physLoad;
    DWORD idle=g_latestSnapshot.idleSeconds;time_t ut=time(nullptr)-g_tStartTime;
    ULONGLONG tp=g_latestSnapshot.totalPhys,ap=g_latestSnapshot.availPhys;
    LeaveCriticalSection(&g_csData);

    COLORREF pfClr=pf>85?kRed:pf>60?kYellow:kGreen,phClr=ph>90?kRed:ph>70?kYellow:kGreen;
    int cardW=(cw-48)/4,cy=rc.top+36;

    struct{const char*label;char value[32];COLORREF clr;char sub[128];}cards[4];
    snprintf(cards[0].value,sizeof(cards[0].value),"%lu%%",pf);cards[0].clr=pfClr;cards[0].label=i18n::CARD_PF_LABEL;
    snprintf(cards[0].sub,sizeof(cards[0].sub),i18n.Get(i18n::CARD_PF_SUB).c_str(),PAGE_FILE_THRESHOLD_PCT);
    snprintf(cards[1].value,sizeof(cards[1].value),"%lu%%",ph);cards[1].clr=phClr;cards[1].label=i18n::CARD_PH_LABEL;
    snprintf(cards[1].sub,sizeof(cards[1].sub),i18n.Get(i18n::CARD_PH_SUB).c_str(),FmtMB(ap).c_str(),FmtMB(tp).c_str());
    snprintf(cards[2].value,sizeof(cards[2].value),"%s",FmtDuration(idle).c_str());cards[2].clr=kAccent;cards[2].label=i18n::CARD_IDLE_LABEL;
    snprintf(cards[2].sub,sizeof(cards[2].sub),i18n.Get(i18n::CARD_IDLE_SUB).c_str(),IDLE_THRESHOLD_SEC/60);
    snprintf(cards[3].value,sizeof(cards[3].value),"%s",FmtDuration(ut).c_str());cards[3].clr=kGreen;cards[3].label=i18n::CARD_UPTIME_LABEL;
    snprintf(cards[3].sub,sizeof(cards[3].sub),i18n.Get(i18n::CARD_UPTIME_SUB).c_str(),g_httpPort,DB_FILE_NAME);

    for(int i=0;i<4;i++){int cx=rc.left+12+i*(cardW+8);
        RECT cr={cx,cy,cx+cardW,cy+46};HBRUSH hCb=CreateSolidBrush(kCard2);HPEN hCp=CreatePen(PS_SOLID,1,kBorderLt);
        SelectObject(hdc,hCb);SelectObject(hdc,hCp);RoundRect(hdc,cr.left,cr.top,cr.right,cr.bottom,6,6);
        DeleteObject(hCb);DeleteObject(hCp);
        SetTextColor(hdc,kMuted);if(m_hFont)SelectObject(hdc,m_hFont);
        DrawTextA(hdc,(char*)i18n.Get(cards[i].label).c_str(),-1,
            &_R(cr.left+8,cr.top+4,cr.right-8,cr.top+20),DT_LEFT|DT_VCENTER|DT_SINGLELINE);
        SetTextColor(hdc,cards[i].clr);if(m_hTitleFont)SelectObject(hdc,m_hTitleFont);
        DrawTextA(hdc,cards[i].value,-1,&_R(cr.left+8,cr.top+20,cr.right-8,cr.top+44),DT_LEFT|DT_VCENTER|DT_SINGLELINE);
        SetTextColor(hdc,kMuted);if(m_hFont)SelectObject(hdc,m_hFont);
        DrawTextA(hdc,cards[i].sub,-1,&_R(cr.left+8,cr.top+48,cr.right-8,cr.top+68),DT_LEFT|DT_VCENTER|DT_SINGLELINE);}
}

// ============================================================================
// Data refresh
// ============================================================================
void VMDesktopApp::RefreshAll(){RefreshProcessList();RefreshOverview();RefreshDatabasePanel();
    RefreshChart();RefreshAnomalyList();RefreshSuspiciousList();RefreshStatusBar();}
void VMDesktopApp::RefreshProcessList(){if(!m_hProcList)return;ClearLV(m_hProcList);
    EnterCriticalSection(&g_csData);
    for(int i=0;i<g_latestSnapshot.numProcesses;i++){ProcessInfo*p=&g_latestSnapshot.topProcesses[i];
        char b[6][64];snprintf(b[0],sizeof(b[0]),"%d",i+1);snprintf(b[1],sizeof(b[1]),"%lu",p->pid);
        snprintf(b[2],sizeof(b[2]),"%s",p->name);snprintf(b[3],sizeof(b[3]),"%s",FmtMB(p->commitSize).c_str());
        snprintf(b[4],sizeof(b[4]),"%s",FmtMB(p->workingSet).c_str());
        snprintf(b[5],sizeof(b[5]),p->growthRateMBps>0.01?"+%.1f MB/s":"-",p->growthRateMBps);
        for(int c=0;c<6;c++)AddLVItem(m_hProcList,i,c,b[c]);}
    LeaveCriticalSection(&g_csData);}
void VMDesktopApp::RefreshAnomalyList(){if(!m_hAnomList)return;ClearLV(m_hAnomList);VMI18n&i18n=VMI18n::Instance();
    EnterCriticalSection(&g_csData);
    for(int i=0;i<g_anomalyCount;i++){AnomalyAlert&a=g_anomalies[i];char b[6][256];
        snprintf(b[0],sizeof(b[0]),"%s",FmtTime(a.timestamp).c_str());
        const char*types[]={i18n.Get(i18n::ANOM_CPU_HOG).c_str(),i18n.Get(i18n::ANOM_MEM_LEAK).c_str(),
            i18n.Get(i18n::ANOM_MEM_HOG).c_str(),i18n.Get(i18n::ANOM_GPU_HOG).c_str(),i18n.Get(i18n::ANOM_SUSPICIOUS).c_str()};
        snprintf(b[1],sizeof(b[1]),"%s",a.type<=ANOMALY_SUSPICIOUS?types[a.type]:i18n.Get(i18n::ANOM_UNKNOWN).c_str());
        snprintf(b[2],sizeof(b[2]),"%lu",a.pid);snprintf(b[3],sizeof(b[3]),"%s",a.procName);
        snprintf(b[4],sizeof(b[4]),"%.1f",a.value);snprintf(b[5],sizeof(b[5]),"%s",a.description);
        for(int c=0;c<6;c++)AddLVItem(m_hAnomList,i,c,b[c]);}
    LeaveCriticalSection(&g_csData);}
void VMDesktopApp::RefreshSuspiciousList(){if(!m_hSuspList)return;ClearLV(m_hSuspList);
    EnterCriticalSection(&g_csData);
    for(int i=0;i<g_suspProcCount;i++){SuspiciousProc&sp=g_suspProcs[i];char b[9][128];
        snprintf(b[0],sizeof(b[0]),"%lu",sp.pid);snprintf(b[1],sizeof(b[1]),"%s",sp.name);
        snprintf(b[2],sizeof(b[2]),"%s",FmtMB(sp.firstCommit).c_str());snprintf(b[3],sizeof(b[3]),"%s",FmtMB(sp.lastCommit).c_str());
        SIZE_T gb=sp.lastCommit>sp.firstCommit?(sp.lastCommit-sp.firstCommit):0;snprintf(b[4],sizeof(b[4]),"%s",FmtMB(gb).c_str());
        snprintf(b[5],sizeof(b[5]),"%.1f MB/s",sp.peakGrowthRate);snprintf(b[6],sizeof(b[6]),"%s",FmtTime(sp.firstSeen).c_str());
        snprintf(b[7],sizeof(b[7]),"%s",FmtTime(sp.lastSeen).c_str());snprintf(b[8],sizeof(b[8]),"%d",sp.alertCount);
        for(int c=0;c<9;c++)AddLVItem(m_hSuspList,i,c,b[c]);}
    LeaveCriticalSection(&g_csData);}
void VMDesktopApp::RefreshOverview(){if(m_hOverviewPane)InvalidateRect(m_hOverviewPane,nullptr,TRUE);
    if(m_hActionLog){ClearLV(m_hActionLog);EnterCriticalSection(&g_csData);
        for(int i=0;i<g_actionCount;i++){ActionRecord&a=g_actions[i];char b[6][512];
            snprintf(b[0],sizeof(b[0]),"%s",FmtTime(a.timestamp).c_str());snprintf(b[1],sizeof(b[1]),"%lu%%",a.pageFileBefore);
            snprintf(b[2],sizeof(b[2]),"%lu%%",a.pageFileAfter);snprintf(b[3],sizeof(b[3]),"%d",a.trimmedCount);
            snprintf(b[4],sizeof(b[4]),"%d",a.failedCount);snprintf(b[5],sizeof(b[5]),"%s [%s%d%%]",a.description,
                (int)a.pageFileBefore>(int)a.pageFileAfter?"-":"+",abs((int)a.pageFileAfter-(int)a.pageFileBefore));
            for(int c=0;c<6;c++)AddLVItem(m_hActionLog,i,c,b[c]);}
        LeaveCriticalSection(&g_csData);}}
void VMDesktopApp::RefreshDatabasePanel(){if(m_hDbPanel)InvalidateRect(m_hDbPanel,nullptr,TRUE);}
void VMDesktopApp::RefreshChart(){if(m_hChartArea)InvalidateRect(m_hChartArea,nullptr,TRUE);}
void VMDesktopApp::RefreshStatusBar(){if(!m_hStatus)return;VMI18n&i18n=VMI18n::Instance();
    DWORD pf=g_latestSnapshot.pageFilePct,ph=g_latestSnapshot.physLoad,idle=g_latestSnapshot.idleSeconds;
    time_t ut=time(nullptr)-g_tStartTime;char buf[512];
    int len=snprintf(buf,sizeof(buf),i18n.Get(i18n::STATUS_FMT).c_str(),pf,ph,idle,g_httpPort);
    snprintf(buf+len,sizeof(buf)-len,"%s",FmtDuration(ut).c_str());SendMessageA(m_hStatus,SB_SETTEXTA,0,(LPARAM)buf);}

// ============================================================================
// Database export
// ============================================================================
void VMDesktopApp::ExportDatabaseCSV(){VMI18n&i18n=VMI18n::Instance();
    OPENFILENAMEA ofn={};char fp[MAX_PATH]="vm_export.csv";
    ofn.lStructSize=sizeof(ofn);ofn.hwndOwner=m_hWnd;
    ofn.lpstrFilter="CSV Files (*.csv)\0*.csv\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile=fp;ofn.nMaxFile=MAX_PATH;ofn.lpstrTitle="Export Database";ofn.Flags=OFN_OVERWRITEPROMPT;
    if(!GetSaveFileNameA(&ofn))return;
    HANDLE hFile=CreateFileA(fp,GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(hFile==INVALID_HANDLE_VALUE){MessageBoxA(m_hWnd,"Cannot create file.","Export Error",MB_OK|MB_ICONERROR);return;}
    const char*header="Timestamp,PageFile%,PhysLoad%,TotalPhysMB,AvailPhysMB,TotalPFMB,AvailPFMB,IdleSec,ProcessCount\r\n";
    DWORD w;WriteFile(hFile,header,(DWORD)strlen(header),&w,nullptr);
    EnterCriticalSection(&g_csData);
    for(int i=0;i<g_snapshotCount;i++){MemorySnapshot&s=g_snapshots[i];char row[512];char tb[32];
        struct tm*tm=localtime(&s.timestamp);strftime(tb,sizeof(tb),"%Y-%m-%d %H:%M:%S",tm);
        int len=snprintf(row,sizeof(row),"%s,%lu,%lu,%I64u,%I64u,%I64u,%I64u,%lu,%d\r\n",tb,s.pageFilePct,s.physLoad,
            (unsigned long long)(s.totalPhys/(1024*1024)),(unsigned long long)(s.availPhys/(1024*1024)),
            (unsigned long long)(s.totalPageFile/(1024*1024)),(unsigned long long)(s.availPageFile/(1024*1024)),
            s.idleSeconds,s.numProcesses);
        WriteFile(hFile,row,(DWORD)len,&w,nullptr);}
    LeaveCriticalSection(&g_csData);CloseHandle(hFile);
    std::string msg=i18n.Format(i18n::DB_EXPORT_DONE,fp);
    MessageBoxA(m_hWnd,msg.c_str(),"VM Manager",MB_OK|MB_ICONINFORMATION);}

// ============================================================================
// System tray
// ============================================================================
void VMDesktopApp::AddTrayIcon(){VMI18n&i18n=VMI18n::Instance();
    NOTIFYICONDATAW nid={};nid.cbSize=sizeof(nid);nid.hWnd=m_hWnd;nid.uID=kTrayUID;
    nid.uFlags=NIF_ICON|NIF_MESSAGE|NIF_TIP;nid.uCallbackMessage=kTrayMsg;
    nid.hIcon=LoadIconA(nullptr,(LPCSTR)IDI_INFORMATION);lstrcpyW(nid.szTip,i18n.GetW(i18n::TRAY_TIP));
    Shell_NotifyIconW(NIM_ADD,&nid);}
void VMDesktopApp::RemoveTrayIcon(){NOTIFYICONDATAW nid={};nid.cbSize=sizeof(nid);nid.hWnd=m_hWnd;nid.uID=kTrayUID;
    Shell_NotifyIconW(NIM_DELETE,&nid);}
void VMDesktopApp::ShowTrayMenu(){VMI18n&i18n=VMI18n::Instance();
    HMENU hMenu=CreatePopupMenu();AppendMenuW(hMenu,MF_STRING,IDM_SHOW,i18n.GetW(i18n::MENU_SHOW));
    AppendMenuW(hMenu,MF_STRING,IDM_CLEANUP,i18n.GetW(i18n::MENU_CLEANUP));
    AppendMenuW(hMenu,MF_STRING,IDM_EXPORT,i18n.GetW(i18n::MENU_EXPORT));
    AppendMenuW(hMenu,MF_SEPARATOR,0,nullptr);AppendMenuW(hMenu,MF_STRING,IDM_EXIT,i18n.GetW(i18n::MENU_EXIT));
    POINT pt;GetCursorPos(&pt);SetForegroundWindow(m_hWnd);
    TrackPopupMenu(hMenu,TPM_RIGHTBUTTON,pt.x,pt.y,0,m_hWnd,nullptr);DestroyMenu(hMenu);}

// ============================================================================
// Window procedure
// ============================================================================
LRESULT CALLBACK VMDesktopApp::WndProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
    VMDesktopApp*self;
    if(msg==WM_CREATE){auto*cs=(CREATESTRUCT*)lp;self=(VMDesktopApp*)cs->lpCreateParams;
        SetWindowLongPtrW(hwnd,GWLP_USERDATA,(LONG_PTR)self);self->m_hWnd=hwnd;self->OnCreate(hwnd);return 0;}
    self=(VMDesktopApp*)GetWindowLongPtrW(hwnd,GWLP_USERDATA);
    if(!self)return DefWindowProcW(hwnd,msg,wp,lp);
    return self->HandleMessage(hwnd,msg,wp,lp);}

LRESULT VMDesktopApp::HandleMessage(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
    switch(msg){
    case WM_SIZE:OnSize(LOWORD(lp),HIWORD(lp));return 0;
    case WM_COMMAND:OnCommand(LOWORD(wp),HIWORD(wp),(HWND)lp);return 0;
    case WM_NOTIFY:OnNotify((NMHDR*)lp);return 0;
    case WM_TIMER:OnTimer();return 0;
    case kTrayMsg:OnTrayIcon(wp,lp);return 0;
    case WM_SYSCOMMAND:if((wp&0xFFF0)==SC_MINIMIZE){ShowWindow(hwnd,SW_HIDE);VMI18n&i18n=VMI18n::Instance();
        NOTIFYICONDATAW nid={};nid.cbSize=sizeof(nid);nid.hWnd=hwnd;nid.uID=kTrayUID;nid.uFlags=NIF_INFO;
        lstrcpyW(nid.szInfoTitle,L"VM Manager");lstrcpyW(nid.szInfo,i18n.GetW(i18n::TRAY_BALLOON_MIN));
        nid.dwInfoFlags=NIIF_INFO;Shell_NotifyIconW(NIM_MODIFY,&nid);}return 0;
    case WM_CLOSE:ShowWindow(hwnd,SW_HIDE);{VMI18n&i18n=VMI18n::Instance();
        NOTIFYICONDATAW nid={};nid.cbSize=sizeof(nid);nid.hWnd=hwnd;nid.uID=kTrayUID;nid.uFlags=NIF_INFO;
        lstrcpyW(nid.szInfoTitle,L"VM Manager");lstrcpyW(nid.szInfo,i18n.GetW(i18n::TRAY_BALLOON_CLOSE));
        nid.dwInfoFlags=NIIF_INFO;Shell_NotifyIconW(NIM_MODIFY,&nid);}return 0;
    case WM_DESTROY:OnDestroy();return 0;
    case WM_CTLCOLORSTATIC:SetTextColor((HDC)wp,kMuted);SetBkColor((HDC)wp,kBg);return(LRESULT)CreateSolidBrush(kBg);
    case WM_CTLCOLORBTN:SetTextColor((HDC)wp,kText2);SetBkColor((HDC)wp,kCard2);return(LRESULT)CreateSolidBrush(kCard2);
    case WM_CTLCOLORLISTBOX:SetTextColor((HDC)wp,kText2);SetBkColor((HDC)wp,kCard);return(LRESULT)CreateSolidBrush(kCard);
    }return DefWindowProcW(hwnd,msg,wp,lp);}

void VMDesktopApp::OnCreate(HWND){InitCommonControls();CreateChildControls();RefreshAll();}
void VMDesktopApp::OnSize(int w,int h){LayoutChildren(w,h);RefreshStatusBar();}
void VMDesktopApp::OnCommand(WORD id,WORD code,HWND){
    switch(id){case ID_BTN_CLEANUP:CheckAndAct();RefreshAll();break;
        case ID_BTN_EXPORT:ExportDatabaseCSV();break;case ID_BTN_EXIT:DestroyWindow(m_hWnd);break;
        case IDM_SHOW:ShowWindow(m_hWnd,SW_RESTORE);SetForegroundWindow(m_hWnd);break;
        case IDM_CLEANUP:CheckAndAct();RefreshAll();break;
        case IDM_EXPORT:ExportDatabaseCSV();break;case IDM_EXIT:DestroyWindow(m_hWnd);break;
        case ID_CBO_RANGE:if(code==CBN_SELCHANGE){m_chartRange=(int)SendMessageW(m_hCboRange,CB_GETCURSEL,0,0);
            InvalidateRect(m_hChartArea,nullptr,TRUE);}break;
        case ID_BTN_CHART:m_chartMode=!m_chartMode;VMI18n&i18n=VMI18n::Instance();
            SetWindowTextW(m_hBtnChartMode,i18n.GetW(m_chartMode?i18n::BTN_CHART_BAR:i18n::BTN_CHART_LINE));
            InvalidateRect(m_hChartArea,nullptr,TRUE);break;}}
void VMDesktopApp::OnNotify(NMHDR*nm){if(nm->idFrom==ID_TAB&&nm->code==TCN_SELCHANGE){
    int sel=(int)SendMessageA(m_hTab,TCM_GETCURSEL,0,0);if(sel>=0&&sel<TAB_COUNT){ShowTab((Tab)sel);RefreshAll();}}}
void VMDesktopApp::OnTimer(){RefreshAll();}
void VMDesktopApp::OnTrayIcon(WPARAM wp,LPARAM lp){if(LOWORD(lp)==WM_RBUTTONUP)ShowTrayMenu();
    else if(LOWORD(lp)==WM_LBUTTONDBLCLK){ShowWindow(m_hWnd,SW_RESTORE);SetForegroundWindow(m_hWnd);}}
void VMDesktopApp::OnDestroy(){KillTimer(m_hWnd,kRefreshTimer);RemoveTrayIcon();m_running=false;PostQuitMessage(0);}

// ============================================================================
// Sub-window procedures
// ============================================================================
LRESULT CALLBACK VMDesktopApp::ChartWndProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
    if(msg==WM_PAINT){PAINTSTRUCT ps;HDC hdc=BeginPaint(hwnd,&ps);RECT rc;GetClientRect(hwnd,&rc);
        HWND parent=GetParent(hwnd);VMDesktopApp*app=(VMDesktopApp*)GetWindowLongPtrW(parent,GWLP_USERDATA);
        if(app){if(app->m_chartMode==0)app->DrawLineChart(hdc,rc);else app->DrawBarChart(hdc,rc);}
        EndPaint(hwnd,&ps);return 0;}if(msg==WM_ERASEBKGND)return 1;
    return DefWindowProcW(hwnd,msg,wp,lp);}
LRESULT CALLBACK VMDesktopApp::OverviewWndProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
    if(msg==WM_PAINT){PAINTSTRUCT ps;HDC hdc=BeginPaint(hwnd,&ps);RECT rc;GetClientRect(hwnd,&rc);
        HWND parent=GetParent(hwnd);VMDesktopApp*app=(VMDesktopApp*)GetWindowLongPtrW(parent,GWLP_USERDATA);
        if(app){rc.bottom=90;app->DrawOverviewCards(hdc,rc);}EndPaint(hwnd,&ps);return 0;}
    if(msg==WM_ERASEBKGND)return 1;return DefWindowProcW(hwnd,msg,wp,lp);}

// ============================================================================
// Formatting helpers
// ============================================================================
std::string VMDesktopApp::FmtMB(ULONGLONG b){char buf[32];
    if(b>=1073741824ULL)snprintf(buf,sizeof(buf),"%.1f GB",b/1073741824.0);
    else snprintf(buf,sizeof(buf),"%I64u MB",(unsigned long long)(b/1048576));return buf;}
std::string VMDesktopApp::FmtTime(time_t t){char buf[32];struct tm*tm=localtime(&t);
    strftime(buf,sizeof(buf),"%Y-%m-%d %H:%M",tm);return buf;}
std::string VMDesktopApp::FmtDuration(time_t sec){char buf[32];
    if(sec<60)snprintf(buf,sizeof(buf),"%I64ds",(long long)sec);
    else if(sec<3600)snprintf(buf,sizeof(buf),"%I64dm",(long long)(sec/60));
    else snprintf(buf,sizeof(buf),"%I64dh %I64dm",(long long)(sec/3600),(long long)((sec%3600)/60));return buf;}
