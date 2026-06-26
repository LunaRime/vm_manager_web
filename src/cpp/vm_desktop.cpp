/**
 * vm_desktop.cpp — C++ Desktop GUI v2 (Unicode-clean, DPI-aware)
 *
 * All text: DrawTextW + GetW().  All lists: LVM_*W + GetW().
 * WM_CLOSE → destroy.  SC_MINIMIZE → hide to tray.
 */
#include "vm_desktop.hpp"
#include <commdlg.h>

#define CLEARTYPE_QUALITY 5
#define ANTIALIASED_QUALITY 4

static RECT& _R(int l,int t,int r,int btm){static RECT buf[8];static int n;
    RECT& rc=buf[n];n=(n+1)%8;rc.left=l;rc.top=t;rc.right=r;rc.bottom=btm;return rc;}

static std::wstring ToWide(const char* u){if(!u)return L"?";
    int n=MultiByteToWideChar(CP_UTF8,0,u,-1,nullptr,0);std::wstring w;
    w.resize(n>0?n-1:0);if(n>1)MultiByteToWideChar(CP_UTF8,0,u,-1,&w[0],n);return w;}

// ============================================================================
const COLORREF VMDesktopApp::kBg=RGB(13,17,23),VMDesktopApp::kCard=RGB(22,27,34);
const COLORREF VMDesktopApp::kCard2=RGB(30,35,42),VMDesktopApp::kBorder=RGB(48,54,61);
const COLORREF VMDesktopApp::kBorderLt=RGB(58,64,71),VMDesktopApp::kText=RGB(225,230,237);
const COLORREF VMDesktopApp::kText2=RGB(201,209,217),VMDesktopApp::kMuted=RGB(139,148,158);
const COLORREF VMDesktopApp::kAccent=RGB(88,166,255),VMDesktopApp::kGreen=RGB(63,185,80);
const COLORREF VMDesktopApp::kRed=RGB(248,81,73),VMDesktopApp::kOrange=RGB(255,159,50);
const COLORREF VMDesktopApp::kYellow=RGB(210,153,34);
const COLORREF VMDesktopApp::kPfColor=RGB(88,166,255),VMDesktopApp::kPhColor=RGB(63,185,80);
const WCHAR* VMDesktopApp::kMainClass=L"VMDskV52W";
const WCHAR* VMDesktopApp::kChartClass=L"VMChV52W";
const WCHAR* VMDesktopApp::kOverviewClass=L"VMOvV52W";

// ============================================================================
VMDesktopApp::VMDesktopApp():m_hInst(nullptr),m_hWnd(nullptr),m_hFont(nullptr),
    m_hTitleFont(nullptr),m_hMonoFont(nullptr),m_hTab(nullptr),m_hBtnCleanup(nullptr),
    m_hBtnExport(nullptr),m_hBtnExit(nullptr),m_hOverviewPane(nullptr),m_hProcList(nullptr),
    m_hDbPanel(nullptr),m_hChartArea(nullptr),m_hAnomList(nullptr),m_hSuspList(nullptr),
    m_hActionLog(nullptr),m_hCboRange(nullptr),m_hLblRange(nullptr),m_hBtnChartMode(nullptr),
    m_hStatus(nullptr),m_activeTab(TAB_OV),m_chartRange(CHART_WEEK),m_chartMode(0),m_running(false){}
VMDesktopApp::~VMDesktopApp(){if(m_hFont)DeleteObject(m_hFont);
    if(m_hTitleFont)DeleteObject(m_hTitleFont);if(m_hMonoFont)DeleteObject(m_hMonoFont);}

int VMDesktopApp::Run(HINSTANCE hInst,int nCmdShow){m_hInst=hInst;
    if(!RegisterWindowClass())return 1;if(!CreateMainWindow(nCmdShow))return 1;
    g_bDesktop=TRUE;m_running=true;MSG msg;
    while(m_running&&GetMessageW(&msg,nullptr,0,0)){TranslateMessage(&msg);DispatchMessageW(&msg);}
    g_bRunning=FALSE;return 0;}

bool VMDesktopApp::RegisterWindowClass(){WNDCLASSW wc={};
    wc.lpfnWndProc=WndProc;wc.hInstance=m_hInst;wc.hCursor=LoadCursorA(nullptr,(LPCSTR)IDC_ARROW);
    wc.hbrBackground=CreateSolidBrush(kBg);wc.lpszClassName=kMainClass;
    wc.hIcon=LoadIconA(nullptr,(LPCSTR)IDI_APPLICATION);
    if(!RegisterClassW(&wc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return false;
    wc.lpfnWndProc=ChartWndProc;wc.hbrBackground=CreateSolidBrush(kCard);wc.lpszClassName=kChartClass;
    if(!RegisterClassW(&wc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return false;
    wc.lpfnWndProc=OverviewWndProc;wc.hbrBackground=CreateSolidBrush(kBg);wc.lpszClassName=kOverviewClass;
    if(!RegisterClassW(&wc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return false;return true;}

bool VMDesktopApp::CreateMainWindow(int nCmdShow){VMI18n& i18n=VMI18n::Instance();
    m_hWnd=CreateWindowExW(0,kMainClass,i18n.GetW(i18n::APP_TITLE),
        WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,1100,740,
        nullptr,nullptr,m_hInst,this);if(!m_hWnd)return false;
    HMODULE hDwm=LoadLibraryA("dwmapi.dll");
    if(hDwm){typedef HRESULT(WINAPI*Pfn)(HWND,DWORD,LPCVOID,DWORD);
        auto pfn=(Pfn)GetProcAddress(hDwm,"DwmSetWindowAttribute");
        if(pfn){BOOL d=TRUE;pfn(m_hWnd,20,&d,sizeof(d));}}
    ShowWindow(m_hWnd,nCmdShow);UpdateWindow(m_hWnd);return true;}

// ============================================================================
void VMDesktopApp::CreateChildControls(){VMI18n& i18n=VMI18n::Instance();
    RECT rc;GetClientRect(m_hWnd,&rc);int cw=rc.right,ch=rc.bottom;
    LocaleId loc=i18n.GetLocale();
    const char* face=(loc==LOC_ZH_TW)?"Microsoft JhengHei":"Microsoft YaHei";
    m_hFont=CreateFontA(14,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,
        DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,FF_DONTCARE,face);
    m_hTitleFont=CreateFontA(18,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,
        DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,FF_DONTCARE,face);
    m_hMonoFont=CreateFontA(13,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,
        DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,FF_DONTCARE,"Consolas");

    /* Tab — TCITEMW */
    m_hTab=CreateWindowExW(0,WC_TABCONTROLW,nullptr,WS_CHILD|WS_VISIBLE|TCS_FIXEDWIDTH,
        8,6,cw-16,28,m_hWnd,(HMENU)ID_TAB,m_hInst,nullptr);
    SendMessageW(m_hTab,WM_SETFONT,(WPARAM)m_hFont,TRUE);
    {const char* tks[]={i18n::TAB_OVERVIEW,i18n::TAB_PROCESSES,i18n::TAB_DATABASE,
        i18n::TAB_CHARTS,i18n::TAB_ANOMALIES,i18n::TAB_SUSPICIOUS};
    for(int i=0;i<6;i++){TCITEMW tci={};tci.mask=TCIF_TEXT;
        tci.pszText=(LPWSTR)i18n.GetW(tks[i]);SendMessageW(m_hTab,TCM_INSERTITEMW,i,(LPARAM)&tci);}}

    /* Buttons */
    auto btn=[&](int x,const char* k,int id){HWND h=CreateWindowExW(0,L"BUTTON",
        i18n.GetW(k),WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON|BS_FLAT,
        x,8,110,26,m_hWnd,(HMENU)id,m_hInst,nullptr);
        SendMessageW(h,WM_SETFONT,(WPARAM)m_hFont,TRUE);return h;};
    m_hBtnExport=btn(cw-460,i18n::BTN_EXPORT,ID_BTN_EXPORT);
    m_hBtnCleanup=btn(cw-340,i18n::BTN_CLEANUP,ID_BTN_CLEANUP);
    m_hBtnExit=btn(cw-120,i18n::BTN_EXIT,ID_BTN_EXIT);

    /* Chart controls */
    m_hLblRange=CreateWindowExW(0,L"STATIC",i18n.GetW(i18n::CHART_RANGE_LBL),
        WS_CHILD|SS_LEFT,16,42,100,20,m_hWnd,nullptr,m_hInst,nullptr);
    SendMessageW(m_hLblRange,WM_SETFONT,(WPARAM)m_hFont,TRUE);ShowWindow(m_hLblRange,SW_HIDE);
    m_hCboRange=CreateWindowExW(0,L"COMBOBOX",nullptr,WS_CHILD|CBS_DROPDOWNLIST|WS_VSCROLL,
        120,40,140,200,m_hWnd,(HMENU)ID_CBO_RANGE,m_hInst,nullptr);
    SendMessageW(m_hCboRange,WM_SETFONT,(WPARAM)m_hFont,TRUE);
    {const char* cr[]={i18n::CHART_RANGE_DAY,i18n::CHART_RANGE_WEEK,i18n::CHART_RANGE_MONTH,i18n::CHART_RANGE_YEAR};
    for(int i=0;i<4;i++)SendMessageW(m_hCboRange,CB_ADDSTRING,0,(LPARAM)i18n.GetW(cr[i]));}
    SendMessageW(m_hCboRange,CB_SETCURSEL,1,0);ShowWindow(m_hCboRange,SW_HIDE);
    m_hBtnChartMode=CreateWindowExW(0,L"BUTTON",L"Line",WS_CHILD|BS_PUSHBUTTON|BS_FLAT,
        270,40,60,22,m_hWnd,(HMENU)ID_BTN_CHART,m_hInst,nullptr);
    SendMessageW(m_hBtnChartMode,WM_SETFONT,(WPARAM)m_hFont,TRUE);ShowWindow(m_hBtnChartMode,SW_HIDE);

    /* Overview */
    m_hOverviewPane=CreateWindowExW(0,kOverviewClass,nullptr,WS_CHILD|WS_VISIBLE,
        8,44,cw-16,90,m_hWnd,(HMENU)ID_OV_PANE,m_hInst,nullptr);

    /* ListViews */
    {const char* ph[]={i18n::HDR_RANK,i18n::HDR_PID,i18n::HDR_NAME,i18n::HDR_COMMIT,i18n::HDR_WS,i18n::HDR_GROWTH};
    int pw[]={36,70,200,120,120,110};m_hProcList=CreateLV(8,142,cw-16,ch-190,ph,pw,6,ID_LIST_PROC);}
    {const char* ah[]={i18n::HDR_TIME,i18n::HDR_BEFORE,i18n::HDR_AFTER,i18n::HDR_TRIMMED,i18n::HDR_FAILED,i18n::HDR_DESC};
    int aw[]={130,60,60,60,50,290};m_hActionLog=CreateLV(8,ch-240,cw-16,200,ah,aw,6,ID_LIST_ACTS);}
    {const char* nh[]={i18n::HDR_TIME,i18n::HDR_TYPE,i18n::HDR_PID,i18n::HDR_PROCESS,i18n::HDR_VALUE,i18n::HDR_DESC};
    int nw[]={130,100,55,140,70,360};m_hAnomList=CreateLV(8,44,cw-16,ch-64,nh,nw,6,ID_LIST_ANOM);}
    {const char* sh[]={i18n::HDR_PID,i18n::HDR_NAME,i18n::HDR_FIRST,i18n::HDR_LAST,
        i18n::HDR_GROWTH,i18n::HDR_PEAK_RATE,i18n::HDR_FIRST_SEEN,i18n::HDR_LAST_SEEN,i18n::HDR_ALERTS};
    int sw[]={55,130,100,100,95,90,135,135,50};m_hSuspList=CreateLV(8,44,cw-16,ch-64,sh,sw,9,ID_LIST_SUSP);}

    /* DB panel + Chart */
    m_hDbPanel=CreateWindowExW(0,L"STATIC",nullptr,WS_CHILD|SS_OWNERDRAW,
        8,44,cw-16,ch-64,m_hWnd,(HMENU)ID_DB_PANEL,m_hInst,nullptr);
    m_hChartArea=CreateWindowExW(0,kChartClass,nullptr,WS_CHILD|WS_BORDER,
        8,68,cw-16,ch-100,m_hWnd,(HMENU)ID_CHART_AREA,m_hInst,nullptr);

    /* Status bar */
    m_hStatus=CreateWindowExW(0,STATUSCLASSNAMEW,nullptr,WS_CHILD|WS_VISIBLE|SBARS_SIZEGRIP,
        0,0,0,0,m_hWnd,nullptr,m_hInst,nullptr);
    SendMessageW(m_hStatus,WM_SETFONT,(WPARAM)m_hFont,TRUE);

    ShowTab(TAB_OV);AddTrayIcon();SetTimer(m_hWnd,kRefreshTimer,kRefreshMs,nullptr);}

// ============================================================================
HWND VMDesktopApp::CreateLV(int x,int y,int w,int h,const char**hdrs,int*wds,int n,int id){
    HWND lv=CreateWindowExW(0,WC_LISTVIEWW,nullptr,
        WS_CHILD|WS_VISIBLE|LVS_REPORT|LVS_SINGLESEL|LVS_SHOWSELALWAYS,
        x,y,w,h,m_hWnd,(HMENU)id,m_hInst,nullptr);
    SendMessageW(lv,LVM_SETEXTENDEDLISTVIEWSTYLE,0,LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER|LVS_EX_GRIDLINES);
    VMI18n& i18n=VMI18n::Instance();
    for(int i=0;i<n;i++){LV_COLUMNW c={};c.mask=LVCF_TEXT|LVCF_WIDTH|LVCF_FMT;c.fmt=LVCFMT_LEFT;
        c.pszText=(LPWSTR)i18n.GetW(hdrs[i]);c.cx=wds[i];SendMessageW(lv,LVM_INSERTCOLUMNW,i,(LPARAM)&c);}
    ListView_SetBkColor(lv,kCard);ListView_SetTextBkColor(lv,kCard);ListView_SetTextColor(lv,kText2);return lv;}

void VMDesktopApp::AddLV(HWND lv,int row,int col,const WCHAR*txt){LVITEMW it={};
    it.iSubItem=col;it.pszText=(LPWSTR)txt;
    if(col==0){it.mask=LVIF_TEXT;it.iItem=row;SendMessageW(lv,LVM_INSERTITEMW,0,(LPARAM)&it);}
    else SendMessageW(lv,LVM_SETITEMTEXTW,row,(LPARAM)&it);}
void VMDesktopApp::ClearLV(HWND lv){SendMessageW(lv,LVM_DELETEALLITEMS,0,0);}

// ============================================================================
void VMDesktopApp::ShowTab(Tab t){m_activeTab=t;
    int ov=t==TAB_OV,pr=t==TAB_PROC,db=t==TAB_DB,ch=t==TAB_CHART,an=t==TAB_ANOM,su=t==TAB_SUSP;
    ShowWindow(m_hOverviewPane,ov?SW_SHOW:SW_HIDE);ShowWindow(m_hProcList,(ov||pr)?SW_SHOW:SW_HIDE);
    ShowWindow(m_hActionLog,ov?SW_SHOW:SW_HIDE);ShowWindow(m_hDbPanel,db?SW_SHOW:SW_HIDE);
    ShowWindow(m_hChartArea,ch?SW_SHOW:SW_HIDE);ShowWindow(m_hCboRange,ch?SW_SHOW:SW_HIDE);
    ShowWindow(m_hLblRange,ch?SW_SHOW:SW_HIDE);ShowWindow(m_hBtnChartMode,ch?SW_SHOW:SW_HIDE);
    ShowWindow(m_hAnomList,an?SW_SHOW:SW_HIDE);ShowWindow(m_hSuspList,su?SW_SHOW:SW_HIDE);}

void VMDesktopApp::LayoutChildren(int cw,int ch){int ct=44,cl=8,cr=cw-16;
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
    SendMessageW(m_hStatus,WM_SIZE,0,0);}

// ============================================================================
// GDI — ALL text: DrawTextW (Unicode, no codepage issues)
// ============================================================================
void VMDesktopApp::DrawLineChart(HDC hdc,RECT rc){int w=rc.right-rc.left,h=rc.bottom-rc.top;
    if(w<=0||h<=0)return;VMI18n& i18n=VMI18n::Instance();
    HBRUSH hBg=CreateSolidBrush(kCard);FillRect(hdc,&rc,hBg);DeleteObject(hBg);
    int pt=36,pb=44,pl=60,pr=36,pw=w-pl-pr,ph=h-pt-pb;if(pw<=0||ph<=0)return;

    AggBucket*bk=nullptr;int ct=0,mt=30;
    EnterCriticalSection(&g_csData);
    switch(m_chartRange){case CHART_DAY:bk=g_hourlyBuckets;ct=g_hourlyCount;mt=24;break;
        case CHART_WEEK:bk=g_dailyBuckets;ct=g_dailyCount;mt=7;break;
        case CHART_MONTH:bk=g_dailyBuckets;ct=g_dailyCount;mt=30;break;
        case CHART_YEAR:bk=g_monthlyBuckets;ct=g_monthlyCount;mt=12;break;}
    int take=ct<mt?ct:mt,start=ct-take;if(start<0){start=0;take=ct;}
    AggBucket*plot=(AggBucket*)HeapAlloc(GetProcessHeap(),0,take*sizeof(AggBucket));
    if(plot)memcpy(plot,bk+start,take*sizeof(AggBucket));LeaveCriticalSection(&g_csData);
    if(!plot||take<2){if(plot)HeapFree(GetProcessHeap(),0,plot);
        SetTextColor(hdc,kMuted);SetBkMode(hdc,TRANSPARENT);
        DrawTextW(hdc,i18n.GetW(i18n::DB_NO_DATA),-1,&rc,DT_CENTER|DT_VCENTER|DT_SINGLELINE);return;}

    /* 85% threshold */
    HPEN hThr=CreatePen(PS_DASH,1,kOrange),hOld=(HPEN)SelectObject(hdc,hThr);
    int thrY=pt+ph-(ph*85/100);MoveToEx(hdc,pl,thrY,nullptr);LineTo(hdc,pl+pw,thrY);
    SelectObject(hdc,hOld);DeleteObject(hThr);

    /* Grid */
    HPEN hGd=CreatePen(PS_SOLID,1,kBorder);hOld=(HPEN)SelectObject(hdc,hGd);
    for(int g=0;g<=100;g+=25){int y=pt+ph-(ph*g/100);MoveToEx(hdc,pl,y,nullptr);LineTo(hdc,pl+pw,y);
        WCHAR lb[8];_snwprintf(lb,8,L"%d%%",g);SetTextColor(hdc,kMuted);
        DrawTextW(hdc,lb,-1,&_R(2,y-8,pl-4,y+8),DT_RIGHT|DT_VCENTER|DT_SINGLELINE);}
    SelectObject(hdc,hOld);DeleteObject(hGd);

    /* PF line */
    HPEN hPf=CreatePen(PS_SOLID,3,kPfColor);hOld=(HPEN)SelectObject(hdc,hPf);
    for(int i=0;i<take;i++){double v=plot[i].sampleCount>0?plot[i].pfSum/plot[i].sampleCount:0;
        int x=pl+(int)((double)i/(take-1)*pw),y=pt+ph-(int)(v/100.0*ph);
        if(y<pt)y=pt;if(y>pt+ph)y=pt+ph;i==0?MoveToEx(hdc,x,y,nullptr):LineTo(hdc,x,y);}
    SelectObject(hdc,hOld);DeleteObject(hPf);

    /* PH line */
    HPEN hPh=CreatePen(PS_DASH,2,kPhColor);hOld=(HPEN)SelectObject(hdc,hPh);
    for(int i=0;i<take;i++){double v=plot[i].sampleCount>0?plot[i].phSum/plot[i].sampleCount:0;
        int x=pl+(int)((double)i/(take-1)*pw),y=pt+ph-(int)(v/100.0*ph);
        if(y<pt)y=pt;if(y>pt+ph)y=pt+ph;i==0?MoveToEx(hdc,x,y,nullptr):LineTo(hdc,x,y);}
    SelectObject(hdc,hOld);DeleteObject(hPh);

    /* Dot markers */
    int ds=take>12?take/6:1;
    for(int i=0;i<take;i+=ds){double v=plot[i].sampleCount>0?plot[i].pfMax:0;
        int x=pl+(int)((double)i/(take-1)*pw),y=pt+ph-(int)(v/100.0*ph);
        HBRUSH hD=CreateSolidBrush(kPfColor);HPEN hDP=CreatePen(PS_SOLID,2,kCard);
        SelectObject(hdc,hD);SelectObject(hdc,hDP);Ellipse(hdc,x-4,y-4,x+4,y+4);
        SelectObject(hdc,hOld);DeleteObject(hD);DeleteObject(hDP);
        WCHAR mx[8];_snwprintf(mx,8,L"%.0f%%",v);SetTextColor(hdc,kPfColor);
        DrawTextW(hdc,mx,-1,&_R(x-18,y-22,x+18,y-8),DT_CENTER|DT_BOTTOM|DT_SINGLELINE);}

    /* Legend */
    {RECT lg={pl,8,pl+200,26};HBRUSH hLg=CreateSolidBrush(kCard2);HPEN hLgP=CreatePen(PS_SOLID,1,kBorderLt);
        SelectObject(hdc,hLg);SelectObject(hdc,hLgP);RoundRect(hdc,lg.left,lg.top,lg.right,lg.bottom,8,8);
        DeleteObject(hLg);DeleteObject(hLgP);
        HBRUSH b1=CreateSolidBrush(kPfColor);FillRect(hdc,&_R(pl+8,13,pl+20,21),b1);DeleteObject(b1);
        SetTextColor(hdc,kText);
        DrawTextW(hdc,i18n.GetW(i18n::CHART_LEGEND_PF),-1,&_R(pl+24,11,pl+80,23),DT_LEFT|DT_VCENTER|DT_SINGLELINE);
        HBRUSH b2=CreateSolidBrush(kPhColor);FillRect(hdc,&_R(pl+88,13,pl+100,21),b2);DeleteObject(b2);
        DrawTextW(hdc,i18n.GetW(i18n::CHART_LEGEND_PH),-1,&_R(pl+104,11,pl+170,23),DT_LEFT|DT_VCENTER|DT_SINGLELINE);
        SelectObject(hdc,hOld);}HeapFree(GetProcessHeap(),0,plot);}

void VMDesktopApp::DrawBarChart(HDC hdc,RECT rc){int w=rc.right-rc.left,h=rc.bottom-rc.top;
    if(w<=0||h<=0)return;
    HBRUSH hBg=CreateSolidBrush(kCard);FillRect(hdc,&rc,hBg);DeleteObject(hBg);
    int pt=36,pb=44,pl=60,pr=36,pw=w-pl-pr,ph=h-pt-pb;if(pw<=0||ph<=0)return;

    int mt=12;AggBucket*bk=nullptr;int ct=0;
    EnterCriticalSection(&g_csData);
    switch(m_chartRange){case CHART_DAY:bk=g_hourlyBuckets;ct=g_hourlyCount;mt=12;break;
        case CHART_WEEK:bk=g_dailyBuckets;ct=g_dailyCount;mt=7;break;
        case CHART_MONTH:bk=g_dailyBuckets;ct=g_dailyCount;mt=12;break;
        case CHART_YEAR:bk=g_monthlyBuckets;ct=g_monthlyCount;mt=12;break;}
    int take=ct<mt?ct:mt,start=ct-take;if(start<0){start=0;take=ct;}
    AggBucket*plot=(AggBucket*)HeapAlloc(GetProcessHeap(),0,take*sizeof(AggBucket));
    if(plot)memcpy(plot,bk+start,take*sizeof(AggBucket));LeaveCriticalSection(&g_csData);
    if(!plot||take<1){if(plot)HeapFree(GetProcessHeap(),0,plot);return;}

    HPEN hThr=CreatePen(PS_DASH,1,kOrange),hOld=(HPEN)SelectObject(hdc,hThr);
    int thrY=pt+ph-(ph*85/100);MoveToEx(hdc,pl,thrY,nullptr);LineTo(hdc,pl+pw,thrY);
    SelectObject(hdc,hOld);DeleteObject(hThr);

    HPEN hGd=CreatePen(PS_SOLID,1,kBorder);hOld=(HPEN)SelectObject(hdc,hGd);
    for(int g=0;g<=100;g+=25){int y=pt+ph-(ph*g/100);MoveToEx(hdc,pl,y,nullptr);LineTo(hdc,pl+pw,y);
        WCHAR lb[8];_snwprintf(lb,8,L"%d%%",g);SetTextColor(hdc,kMuted);
        DrawTextW(hdc,lb,-1,&_R(2,y-8,pl-4,y+8),DT_RIGHT|DT_VCENTER|DT_SINGLELINE);}
    SelectObject(hdc,hOld);DeleteObject(hGd);

    int nG=take,bpG=2,gap=12,avail=pw-gap*(nG-1),barW=avail/(nG*bpG);if(barW<4)barW=4;
    for(int i=0;i<take;i++){double pf=plot[i].sampleCount>0?plot[i].pfSum/plot[i].sampleCount:0;
        double phv=plot[i].sampleCount>0?plot[i].phSum/plot[i].sampleCount:0;
        int gx=pl+(int)((double)i/(take-1)*pw),bx=gx-(bpG*barW+gap)/2;
        int pfH=(int)(pf/100.0*ph);if(pfH<2)pfH=2;HBRUSH bPf=CreateSolidBrush(kPfColor);
        FillRect(hdc,&_R(bx,pt+ph-pfH,bx+barW,pt+ph),bPf);DeleteObject(bPf);
        int phH=(int)(phv/100.0*ph);if(phH<2)phH=2;HBRUSH bPh=CreateSolidBrush(kPhColor);
        FillRect(hdc,&_R(bx+barW+2,pt+ph-phH,bx+barW*2+2,pt+ph),bPh);DeleteObject(bPh);}

    SetTextColor(hdc,kMuted);
    for(int i=0;i<take;i++){if(take>8&&i%(take/4)!=0&&i!=take-1)continue;
        int x=pl+(int)((double)i/(take-1)*pw);WCHAR lb[32];
        struct tm*tm=localtime(&plot[i].bucketStart);
        if(m_chartRange==CHART_DAY)wcsftime(lb,32,L"%H:%M",tm);
        else if(m_chartRange==CHART_YEAR)wcsftime(lb,32,L"%b",tm);
        else wcsftime(lb,32,L"%m/%d",tm);
        DrawTextW(hdc,lb,-1,&_R(x-25,pt+ph+6,x+25,pt+ph+22),DT_CENTER|DT_TOP|DT_SINGLELINE);}
    HeapFree(GetProcessHeap(),0,plot);}

void VMDesktopApp::DrawOverviewCards(HDC hdc,RECT rc){VMI18n& i18n=VMI18n::Instance();
    int cw=rc.right-rc.left;HBRUSH hBg=CreateSolidBrush(kCard);HPEN hBdr=CreatePen(PS_SOLID,1,kBorder);
    SelectObject(hdc,hBg);SelectObject(hdc,hBdr);
    Rectangle(hdc,rc.left,rc.top,rc.right,rc.top+90);DeleteObject(hBg);DeleteObject(hBdr);
    SetTextColor(hdc,kText);SetBkMode(hdc,TRANSPARENT);
    if(m_hTitleFont)SelectObject(hdc,m_hTitleFont);
    DrawTextW(hdc,i18n.GetW(i18n::CARD_TITLE),-1,
        &_R(rc.left+12,rc.top+8,rc.right-12,rc.top+30),DT_LEFT|DT_VCENTER|DT_SINGLELINE);

    EnterCriticalSection(&g_csData);
    DWORD pf=g_latestSnapshot.pageFilePct,ph=g_latestSnapshot.physLoad;
    DWORD idle=g_latestSnapshot.idleSeconds;time_t ut=time(nullptr)-g_tStartTime;
    ULONGLONG tp=g_latestSnapshot.totalPhys,ap=g_latestSnapshot.availPhys;
    LeaveCriticalSection(&g_csData);

    COLORREF pfc=pf>85?kRed:pf>60?kYellow:kGreen,phc=ph>90?kRed:ph>70?kYellow:kGreen;
    int cdW=(cw-48)/4,cy=rc.top+36;

    const WCHAR* labels[4];WCHAR vals[4][32];COLORREF clrs[4];WCHAR subs[4][128];
    labels[0]=i18n.GetW(i18n::CARD_PF_LABEL);clrs[0]=pfc;
    _snwprintf(vals[0],32,L"%lu%%",pf);_snwprintf(subs[0],128,L"%S",i18n.Format(i18n::CARD_PF_SUB,PAGE_FILE_THRESHOLD_PCT).c_str());
    labels[1]=i18n.GetW(i18n::CARD_PH_LABEL);clrs[1]=phc;
    _snwprintf(vals[1],32,L"%lu%%",ph);
    {auto a=ToWide(FmtMB(ap).c_str());auto t=ToWide(FmtMB(tp).c_str());
    _snwprintf(subs[1],128,L"%s / %s total",a.c_str(),t.c_str());}
    labels[2]=i18n.GetW(i18n::CARD_IDLE_LABEL);clrs[2]=kAccent;
    _snwprintf(vals[2],32,L"%S",FmtDuration(idle).c_str());
    _snwprintf(subs[2],128,L"%S",i18n.Format(i18n::CARD_IDLE_SUB,IDLE_THRESHOLD_SEC/60).c_str());
    labels[3]=i18n.GetW(i18n::CARD_UPTIME_LABEL);clrs[3]=kGreen;
    _snwprintf(vals[3],32,L"%S",FmtDuration(ut).c_str());
    _snwprintf(subs[3],128,L"%S",i18n.Format(i18n::CARD_UPTIME_SUB,g_httpPort,DB_FILE_NAME).c_str());

    for(int i=0;i<4;i++){int cx=rc.left+12+i*(cdW+8);
        RECT cr={cx,cy,cx+cdW,cy+46};HBRUSH hCb=CreateSolidBrush(kCard2);HPEN hCp=CreatePen(PS_SOLID,1,kBorderLt);
        SelectObject(hdc,hCb);SelectObject(hdc,hCp);RoundRect(hdc,cr.left,cr.top,cr.right,cr.bottom,6,6);
        DeleteObject(hCb);DeleteObject(hCp);
        SetTextColor(hdc,kMuted);if(m_hFont)SelectObject(hdc,m_hFont);
        DrawTextW(hdc,labels[i],-1,&_R(cr.left+8,cr.top+4,cr.right-8,cr.top+20),DT_LEFT|DT_VCENTER|DT_SINGLELINE);
        SetTextColor(hdc,clrs[i]);if(m_hTitleFont)SelectObject(hdc,m_hTitleFont);
        DrawTextW(hdc,vals[i],-1,&_R(cr.left+8,cr.top+20,cr.right-8,cr.top+44),DT_LEFT|DT_VCENTER|DT_SINGLELINE);
        SetTextColor(hdc,kMuted);if(m_hFont)SelectObject(hdc,m_hFont);
        DrawTextW(hdc,subs[i],-1,&_R(cr.left+8,cr.top+48,cr.right-8,cr.top+68),DT_LEFT|DT_VCENTER|DT_SINGLELINE);}}

// ============================================================================
void VMDesktopApp::RefreshAll(){RefreshProcessList();RefreshOverview();
    RefreshDatabasePanel();RefreshChart();RefreshAnomalyList();RefreshSuspiciousList();RefreshStatusBar();}

void VMDesktopApp::RefreshProcessList(){if(!m_hProcList)return;ClearLV(m_hProcList);
    EnterCriticalSection(&g_csData);
    for(int i=0;i<g_latestSnapshot.numProcesses;i++){ProcessInfo*p=&g_latestSnapshot.topProcesses[i];
        WCHAR b[6][64];_snwprintf(b[0],64,L"%d",i+1);_snwprintf(b[1],64,L"%lu",p->pid);
        auto nm=ToWide(p->name);_snwprintf(b[2],64,L"%s",nm.c_str());
        auto cm=ToWide(FmtMB(p->commitSize).c_str());_snwprintf(b[3],64,L"%s",cm.c_str());
        auto ws=ToWide(FmtMB(p->workingSet).c_str());_snwprintf(b[4],64,L"%s",ws.c_str());
        _snwprintf(b[5],64,p->growthRateMBps>0.01?L"+%.1f MB/s":L"-",p->growthRateMBps);
        for(int c=0;c<6;c++)AddLV(m_hProcList,i,c,b[c]);}
    LeaveCriticalSection(&g_csData);}

void VMDesktopApp::RefreshAnomalyList(){if(!m_hAnomList)return;ClearLV(m_hAnomList);VMI18n& i18n=VMI18n::Instance();
    EnterCriticalSection(&g_csData);
    for(int i=0;i<g_anomalyCount;i++){AnomalyAlert&a=g_anomalies[i];WCHAR b[6][256];
        auto ts=ToWide(FmtTime(a.timestamp).c_str());_snwprintf(b[0],256,L"%s",ts.c_str());
        const char* tks[]={i18n::ANOM_CPU_HOG,i18n::ANOM_MEM_LEAK,i18n::ANOM_MEM_HOG,i18n::ANOM_GPU_HOG,i18n::ANOM_SUSPICIOUS};
        lstrcpyW(b[1],i18n.GetW(a.type<=ANOMALY_SUSPICIOUS?tks[a.type]:i18n::ANOM_UNKNOWN));
        _snwprintf(b[2],256,L"%lu",a.pid);
        auto nm=ToWide(a.procName);_snwprintf(b[3],256,L"%s",nm.c_str());
        _snwprintf(b[4],256,L"%.1f",a.value);
        auto ds=ToWide(a.description);_snwprintf(b[5],256,L"%s",ds.c_str());
        for(int c=0;c<6;c++)AddLV(m_hAnomList,i,c,b[c]);}
    LeaveCriticalSection(&g_csData);}

void VMDesktopApp::RefreshSuspiciousList(){if(!m_hSuspList)return;ClearLV(m_hSuspList);
    EnterCriticalSection(&g_csData);
    for(int i=0;i<g_suspProcCount;i++){SuspiciousProc&sp=g_suspProcs[i];WCHAR b[9][128];
        _snwprintf(b[0],128,L"%lu",sp.pid);
        auto nm=ToWide(sp.name);_snwprintf(b[1],128,L"%s",nm.c_str());
        auto fc=ToWide(FmtMB(sp.firstCommit).c_str());_snwprintf(b[2],128,L"%s",fc.c_str());
        auto lc=ToWide(FmtMB(sp.lastCommit).c_str());_snwprintf(b[3],128,L"%s",lc.c_str());
        SIZE_T gb=sp.lastCommit>sp.firstCommit?(sp.lastCommit-sp.firstCommit):0;
        auto gr=ToWide(FmtMB(gb).c_str());_snwprintf(b[4],128,L"%s",gr.c_str());
        _snwprintf(b[5],128,L"%.1f MB/s",sp.peakGrowthRate);
        auto fs=ToWide(FmtTime(sp.firstSeen).c_str());_snwprintf(b[6],128,L"%s",fs.c_str());
        auto ls=ToWide(FmtTime(sp.lastSeen).c_str());_snwprintf(b[7],128,L"%s",ls.c_str());
        _snwprintf(b[8],128,L"%d",sp.alertCount);
        for(int c=0;c<9;c++)AddLV(m_hSuspList,i,c,b[c]);}
    LeaveCriticalSection(&g_csData);}

void VMDesktopApp::RefreshOverview(){if(m_hOverviewPane)InvalidateRect(m_hOverviewPane,nullptr,TRUE);
    if(m_hActionLog){ClearLV(m_hActionLog);EnterCriticalSection(&g_csData);
        for(int i=0;i<g_actionCount;i++){ActionRecord&a=g_actions[i];WCHAR b[6][512];
            auto ts=ToWide(FmtTime(a.timestamp).c_str());_snwprintf(b[0],512,L"%s",ts.c_str());
            _snwprintf(b[1],512,L"%lu%%",a.pageFileBefore);_snwprintf(b[2],512,L"%lu%%",a.pageFileAfter);
            _snwprintf(b[3],512,L"%d",a.trimmedCount);_snwprintf(b[4],512,L"%d",a.failedCount);
            auto ds=ToWide(a.description);_snwprintf(b[5],512,L"%s [%s%d%%]",ds.c_str(),
                (int)a.pageFileBefore>(int)a.pageFileAfter?L"-":L"+",
                abs((int)a.pageFileAfter-(int)a.pageFileBefore));
            for(int c=0;c<6;c++)AddLV(m_hActionLog,i,c,b[c]);}
        LeaveCriticalSection(&g_csData);}}
void VMDesktopApp::RefreshDatabasePanel(){if(m_hDbPanel)InvalidateRect(m_hDbPanel,nullptr,TRUE);}
void VMDesktopApp::RefreshChart(){if(m_hChartArea)InvalidateRect(m_hChartArea,nullptr,TRUE);}
void VMDesktopApp::RefreshStatusBar(){if(!m_hStatus)return;VMI18n& i18n=VMI18n::Instance();
    DWORD pf=g_latestSnapshot.pageFilePct,ph=g_latestSnapshot.physLoad,idle=g_latestSnapshot.idleSeconds;
    time_t ut=time(nullptr)-g_tStartTime;
    WCHAR buf[512];_snwprintf(buf,512,L"%S%S",
        i18n.Format(i18n::STATUS_FMT,pf,ph,idle,g_httpPort).c_str(),FmtDuration(ut).c_str());
    SendMessageW(m_hStatus,SB_SETTEXTW,0,(LPARAM)buf);}

// ============================================================================
void VMDesktopApp::ExportDatabaseCSV(){VMI18n& i18n=VMI18n::Instance();
    OPENFILENAMEA ofn={};char fp[MAX_PATH]="vm_export.csv";
    ofn.lStructSize=sizeof(ofn);ofn.hwndOwner=m_hWnd;
    ofn.lpstrFilter="CSV Files (*.csv)\0*.csv\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile=fp;ofn.nMaxFile=MAX_PATH;ofn.lpstrTitle="Export Database";ofn.Flags=OFN_OVERWRITEPROMPT;
    if(!GetSaveFileNameA(&ofn))return;
    HANDLE hFile=CreateFileA(fp,GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(hFile==INVALID_HANDLE_VALUE){MessageBoxW(m_hWnd,L"Cannot create file.",L"Error",MB_OK|MB_ICONERROR);return;}
    const char* hdr="Timestamp,PageFile%,PhysLoad%,TotalPhysMB,AvailPhysMB,TotalPFMB,AvailPFMB,IdleSec,ProcessCount\r\n";
    DWORD wr;WriteFile(hFile,hdr,(DWORD)strlen(hdr),&wr,nullptr);
    EnterCriticalSection(&g_csData);
    for(int i=0;i<g_snapshotCount;i++){MemorySnapshot&s=g_snapshots[i];char row[512],tb[32];
        struct tm*tm=localtime(&s.timestamp);strftime(tb,sizeof(tb),"%Y-%m-%d %H:%M:%S",tm);
        int n=snprintf(row,sizeof(row),"%s,%lu,%lu,%I64u,%I64u,%I64u,%I64u,%lu,%d\r\n",tb,
            s.pageFilePct,s.physLoad,(unsigned long long)(s.totalPhys/(1024*1024)),
            (unsigned long long)(s.availPhys/(1024*1024)),(unsigned long long)(s.totalPageFile/(1024*1024)),
            (unsigned long long)(s.availPageFile/(1024*1024)),s.idleSeconds,s.numProcesses);
        WriteFile(hFile,row,(DWORD)n,&wr,nullptr);}
    LeaveCriticalSection(&g_csData);CloseHandle(hFile);
    auto m=ToWide(i18n.Format(i18n::DB_EXPORT_DONE,fp).c_str());
    MessageBoxW(m_hWnd,m.c_str(),L"VM Manager",MB_OK|MB_ICONINFORMATION);}

// ============================================================================
void VMDesktopApp::AddTrayIcon(){VMI18n& i18n=VMI18n::Instance();
    NOTIFYICONDATAW nid={};nid.cbSize=sizeof(nid);nid.hWnd=m_hWnd;nid.uID=kTrayUID;
    nid.uFlags=NIF_ICON|NIF_MESSAGE|NIF_TIP;nid.uCallbackMessage=kTrayMsg;
    nid.hIcon=LoadIconA(nullptr,(LPCSTR)IDI_INFORMATION);lstrcpyW(nid.szTip,i18n.GetW(i18n::TRAY_TIP));
    Shell_NotifyIconW(NIM_ADD,&nid);}
void VMDesktopApp::RemoveTrayIcon(){NOTIFYICONDATAW nid={};nid.cbSize=sizeof(nid);
    nid.hWnd=m_hWnd;nid.uID=kTrayUID;Shell_NotifyIconW(NIM_DELETE,&nid);}
void VMDesktopApp::ShowTrayMenu(){VMI18n& i18n=VMI18n::Instance();
    HMENU hMenu=CreatePopupMenu();AppendMenuW(hMenu,MF_STRING,IDM_SHOW,i18n.GetW(i18n::MENU_SHOW));
    AppendMenuW(hMenu,MF_STRING,IDM_CLEANUP,i18n.GetW(i18n::MENU_CLEANUP));
    AppendMenuW(hMenu,MF_STRING,IDM_EXPORT,i18n.GetW(i18n::MENU_EXPORT));
    AppendMenuW(hMenu,MF_SEPARATOR,0,nullptr);AppendMenuW(hMenu,MF_STRING,IDM_EXIT,i18n.GetW(i18n::MENU_EXIT));
    POINT pt;GetCursorPos(&pt);SetForegroundWindow(m_hWnd);
    TrackPopupMenu(hMenu,TPM_RIGHTBUTTON,pt.x,pt.y,0,m_hWnd,nullptr);DestroyMenu(hMenu);}

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
    case WM_SYSCOMMAND:
        if((wp&0xFFF0)==SC_MINIMIZE){ShowWindow(hwnd,SW_HIDE);
            VMI18n& i18n=VMI18n::Instance();NOTIFYICONDATAW nid={};nid.cbSize=sizeof(nid);
            nid.hWnd=hwnd;nid.uID=kTrayUID;nid.uFlags=NIF_INFO;
            lstrcpyW(nid.szInfoTitle,L"VM Manager");lstrcpyW(nid.szInfo,i18n.GetW(i18n::TRAY_BALLOON_MIN));
            nid.dwInfoFlags=NIIF_INFO;Shell_NotifyIconW(NIM_MODIFY,&nid);return 0;}
        break;
    case WM_CLOSE:DestroyWindow(hwnd);return 0;
    case WM_DESTROY:OnDestroy();return 0;
    case WM_CTLCOLORSTATIC:SetTextColor((HDC)wp,kMuted);SetBkColor((HDC)wp,kBg);
        return(LRESULT)CreateSolidBrush(kBg);
    case WM_CTLCOLORBTN:SetTextColor((HDC)wp,kText2);SetBkColor((HDC)wp,kCard2);
        return(LRESULT)CreateSolidBrush(kCard2);
    case WM_CTLCOLORLISTBOX:SetTextColor((HDC)wp,kText2);SetBkColor((HDC)wp,kCard);
        return(LRESULT)CreateSolidBrush(kCard);
    }
    return DefWindowProcW(hwnd,msg,wp,lp);}

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
        case ID_BTN_CHART:m_chartMode=!m_chartMode;VMI18n& i18n=VMI18n::Instance();
            SetWindowTextW(m_hBtnChartMode,i18n.GetW(m_chartMode?i18n::BTN_CHART_BAR:i18n::BTN_CHART_LINE));
            InvalidateRect(m_hChartArea,nullptr,TRUE);break;}}
void VMDesktopApp::OnNotify(NMHDR*nm){if(nm->idFrom==ID_TAB&&nm->code==TCN_SELCHANGE){
    int sel=(int)SendMessageW(m_hTab,TCM_GETCURSEL,0,0);if(sel>=0&&sel<TAB_COUNT){ShowTab((Tab)sel);RefreshAll();}}}
void VMDesktopApp::OnTimer(){RefreshAll();}
void VMDesktopApp::OnTrayIcon(WPARAM wp,LPARAM lp){if(LOWORD(lp)==WM_RBUTTONUP)ShowTrayMenu();
    else if(LOWORD(lp)==WM_LBUTTONDBLCLK){ShowWindow(m_hWnd,SW_RESTORE);SetForegroundWindow(m_hWnd);}}
void VMDesktopApp::OnDestroy(){KillTimer(m_hWnd,kRefreshTimer);RemoveTrayIcon();
    m_running=false;PostQuitMessage(0);}

LRESULT CALLBACK VMDesktopApp::ChartWndProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
    if(msg==WM_PAINT){PAINTSTRUCT ps;HDC hdc=BeginPaint(hwnd,&ps);RECT rc;GetClientRect(hwnd,&rc);
        HWND p=GetParent(hwnd);VMDesktopApp*a=(VMDesktopApp*)GetWindowLongPtrW(p,GWLP_USERDATA);
        if(a){if(a->m_chartMode==0)a->DrawLineChart(hdc,rc);else a->DrawBarChart(hdc,rc);}
        EndPaint(hwnd,&ps);return 0;}if(msg==WM_ERASEBKGND)return 1;
    return DefWindowProcW(hwnd,msg,wp,lp);}
LRESULT CALLBACK VMDesktopApp::OverviewWndProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
    if(msg==WM_PAINT){PAINTSTRUCT ps;HDC hdc=BeginPaint(hwnd,&ps);RECT rc;GetClientRect(hwnd,&rc);
        HWND p=GetParent(hwnd);VMDesktopApp*a=(VMDesktopApp*)GetWindowLongPtrW(p,GWLP_USERDATA);
        if(a){rc.bottom=90;a->DrawOverviewCards(hdc,rc);}EndPaint(hwnd,&ps);return 0;}
    if(msg==WM_ERASEBKGND)return 1;return DefWindowProcW(hwnd,msg,wp,lp);}

// ============================================================================
std::string VMDesktopApp::FmtMB(ULONGLONG b){char buf[32];
    if(b>=1073741824ULL)snprintf(buf,sizeof(buf),"%.1f GB",b/1073741824.0);
    else snprintf(buf,sizeof(buf),"%I64u MB",(unsigned long long)(b/1048576));return buf;}
std::string VMDesktopApp::FmtTime(time_t t){char buf[32];struct tm*tm=localtime(&t);
    strftime(buf,sizeof(buf),"%Y-%m-%d %H:%M",tm);return buf;}
std::string VMDesktopApp::FmtDuration(time_t s){char buf[32];
    if(s<60)snprintf(buf,sizeof(buf),"%I64ds",(long long)s);
    else if(s<3600)snprintf(buf,sizeof(buf),"%I64dm",(long long)(s/60));
    else snprintf(buf,sizeof(buf),"%I64dh %I64dm",(long long)(s/3600),(long long)((s%3600)/60));return buf;}
