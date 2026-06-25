/**
 * vm_desktop.c — Desktop GUI v2.0
 *   Dark title bar via DwmSetWindowAttribute (Win10 1809+)
 *   Overview: summary cards + process list + action log
 *   Charts: GDI with gradient fill, dot markers, 85% threshold line
 *   Lists: custom-draw colored rows with severity bars
 *   Tray: balloon tip on minimize
 */
#include "vm_common.h"
#include "vm_locale.h"   /* LocaleGet() */

/* ============================================================================
 * DwmApi — dark title bar (Win10 1809+)
 * ============================================================================ */
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
typedef HRESULT (WINAPI *PFN_DwmSetWindowAttribute)(HWND, DWORD, LPCVOID, DWORD);
static void EnableDarkTitleBar(HWND hwnd) {
    HMODULE hDwm = LoadLibraryA("dwmapi.dll");
    if (hDwm) {
        PFN_DwmSetWindowAttribute pfn = (PFN_DwmSetWindowAttribute)
            GetProcAddress(hDwm, "DwmSetWindowAttribute");
        if (pfn) {
            BOOL useDark = TRUE;
            pfn(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &useDark, sizeof(useDark));
        }
        /* dont FreeLibrary — keep DWM loaded for the window lifetime */
    }
}

/* ============================================================================
 * Forward declarations
 * ============================================================================ */
static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
static LRESULT CALLBACK ChartProc(HWND, UINT, WPARAM, LPARAM);
static LRESULT CALLBACK OverviewProc(HWND, UINT, WPARAM, LPARAM);
static void     DrawGdiChart(HDC, RECT, int);
static void     DrawOverviewPanel(HDC, RECT);
static void     RefreshDesktopData(HWND);
static void     AddTrayIcon(HWND);
static void     RemoveTrayIcon(HWND);
static void     ShowTrayMenu(HWND);
static void     ShowTrayBalloon(HWND, const char *title, const char *msg);

/* ============================================================================
 * Raw SendMessage wrappers for ListView (MinGW 6.3.0 compat)
 * ============================================================================ */
static int  LvInsertItem(HWND lv, const LVITEMA *item) { return (int)SendMessageA(lv, LVM_INSERTITEMA, 0, (LPARAM)item); }
static int  LvInsertCol(HWND lv, int iCol, const LV_COLUMNA *col) { return (int)SendMessageA(lv, LVM_INSERTCOLUMNA, (WPARAM)iCol, (LPARAM)col); }
static void LvSetText(HWND lv, int i, int iSubItem, const char *text) {
    LVITEMA lvi; memset(&lvi, 0, sizeof(lvi)); lvi.iSubItem = iSubItem; lvi.pszText = (LPSTR)text;
    SendMessageA(lv, LVM_SETITEMTEXTA, (WPARAM)i, (LPARAM)&lvi);
}
static void LvDelAll(HWND lv) { SendMessageA(lv, LVM_DELETEALLITEMS, 0, 0); }
static void LvSetExSt(HWND lv, DWORD ex) { SendMessageA(lv, LVM_SETEXTENDEDLISTVIEWSTYLE, 0, (LPARAM)ex); }
static int  TcInsItem(HWND tc, int i, const TCITEMA *tci) { return (int)SendMessageA(tc, TCM_INSERTITEMA, (WPARAM)i, (LPARAM)tci); }
static int  TcGetSel(HWND tc) { return (int)SendMessageA(tc, TCM_GETCURSEL, 0, 0); }

/* ============================================================================
 * Globals
 * ============================================================================ */
static HWND   g_hDesktopWnd = NULL, g_hTab = NULL, g_hChartArea = NULL;
static HWND   g_hOverviewPane = NULL;
static HWND   g_hProcList = NULL, g_hAnomalyList = NULL, g_hSuspList = NULL, g_hActionList = NULL;
static HWND   g_hBtnCleanup = NULL, g_hComboRange = NULL, g_hStatusBar = NULL;
static HWND   g_hLblChartRange = NULL;
static int    g_chartRange = CHART_WEEK;
static HFONT  g_hGuiFont = NULL, g_hTitleFont = NULL, g_hMonoFont = NULL;

/* ============================================================================
 * Dark theme colors — GitHub-style
 * ============================================================================ */
#define CLR_BG          RGB(13, 17, 23)
#define CLR_CARD        RGB(22, 27, 34)
#define CLR_CARD2       RGB(30, 35, 42)
#define CLR_BORDER      RGB(48, 54, 61)
#define CLR_BORDER_LT   RGB(58, 64, 71)
#define CLR_TEXT        RGB(225, 230, 237)
#define CLR_TEXT2       RGB(201, 209, 217)
#define CLR_MUTED       RGB(139, 148, 158)
#define CLR_ACCENT      RGB(88, 166, 255)
#define CLR_GREEN       RGB(63, 185, 80)
#define CLR_RED         RGB(248, 81, 73)
#define CLR_ORANGE      RGB(255, 159, 50)
#define CLR_YELLOW      RGB(210, 153, 34)
#define CLR_CHART_PF    RGB(88, 166, 255)
#define CLR_CHART_PH    RGB(63, 185, 80)
#define CLR_GAUGE_HI    RGB(248, 81, 73)
#define CLR_GAUGE_MID   RGB(210, 153, 34)
#define CLR_GAUGE_LO    RGB(63, 185, 80)

/* ============================================================================
 * Formatting helpers
 * ============================================================================ */
static void FmtMB(char *o, int sz, ULONGLONG b) {
    if (b >= 1024ULL*1024*1024) snprintf(o, sz, "%.1f GB", b/(1024.0*1024.0*1024.0));
    else snprintf(o, sz, "%I64u MB", (unsigned long long)(b/(1024*1024)));
}
static void FmtTime(char *o, int sz, time_t t) {
    struct tm *tm = localtime(&t); strftime(o, sz, "%Y-%m-%d %H:%M", tm);
}
static void FmtDur(char *o, int sz, time_t sec) {
    if (sec<60) snprintf(o,sz,"%I64ds",(long long)sec);
    else if (sec<3600) snprintf(o,sz,"%I64dm",(long long)(sec/60));
    else snprintf(o,sz,"%I64dh %I64dm",(long long)(sec/3600),(long long)((sec%3600)/60));
}

/* ============================================================================
 * System tray
 * ============================================================================ */
static void AddTrayIcon(HWND hwnd) {
    NOTIFYICONDATAA nid; memset(&nid,0,sizeof(nid));
    nid.cbSize=sizeof(NOTIFYICONDATAA); nid.hWnd=hwnd; nid.uID=TRAY_UID;
    nid.uFlags=NIF_ICON|NIF_MESSAGE|NIF_TIP;
    nid.uCallbackMessage=WM_TRAYICON; nid.hIcon=LoadIcon(NULL,IDI_INFORMATION);
    lstrcpyA((LPSTR)nid.szTip,"VM Manager — Memory Monitor");
    Shell_NotifyIconA(NIM_ADD,&nid);
}
static void RemoveTrayIcon(HWND hwnd) {
    NOTIFYICONDATAA nid; memset(&nid,0,sizeof(nid));
    nid.cbSize=sizeof(NOTIFYICONDATAA); nid.hWnd=hwnd; nid.uID=TRAY_UID;
    Shell_NotifyIconA(NIM_DELETE,&nid);
}
static void ShowTrayMenu(HWND hwnd) {
    HMENU h=CreatePopupMenu();
    AppendMenuA(h,MF_STRING,IDM_SHOW,L10N(K_MENU_SHOW));
    AppendMenuA(h,MF_STRING,IDM_CLEANUP,L10N(K_MENU_CLEANUP));
    AppendMenuA(h,MF_SEPARATOR,0,NULL);
    AppendMenuA(h,MF_STRING,IDM_EXIT,L10N(K_MENU_EXIT));
    POINT pt; GetCursorPos(&pt); SetForegroundWindow(hwnd);
    TrackPopupMenu(h,TPM_RIGHTBUTTON,pt.x,pt.y,0,hwnd,NULL);
    DestroyMenu(h);
}
static void ShowTrayBalloon(HWND hwnd, const char *title, const char *msg) {
    NOTIFYICONDATAA nid; memset(&nid,0,sizeof(nid));
    nid.cbSize=sizeof(NOTIFYICONDATAA); nid.hWnd=hwnd; nid.uID=TRAY_UID;
    nid.uFlags=NIF_INFO;
    lstrcpyA((LPSTR)nid.szInfoTitle,title);
    lstrcpyA((LPSTR)nid.szInfo,msg);
    nid.dwInfoFlags=NIIF_INFO;
    Shell_NotifyIconA(NIM_MODIFY,&nid);
}

/* ============================================================================
 * GDI Chart — gradient fill, dot markers, 85% threshold line
 * ============================================================================ */
static void DrawGdiChart(HDC hdc, RECT rc, int chartRange) {
    int w=rc.right-rc.left, h=rc.bottom-rc.top;
    if (w<=0||h<=0) return;

    /* Background */
    HBRUSH hBg=CreateSolidBrush(CLR_CARD);
    FillRect(hdc,&rc,hBg); DeleteObject(hBg);

    int pt=36, pb=44, pl=58, pr=36;
    int pw=w-pl-pr, ph=h-pt-pb;
    if (pw<=0||ph<=0) return;

    AggBucket *buckets=NULL; int count=0, maxTake=30;
    EnterCriticalSection(&g_csData);
    switch (chartRange) {
    case CHART_DAY:   buckets=g_hourlyBuckets; count=g_hourlyCount; maxTake=24;  break;
    case CHART_WEEK:  buckets=g_dailyBuckets;  count=g_dailyCount;  maxTake=7;   break;
    case CHART_MONTH: buckets=g_dailyBuckets;  count=g_dailyCount;  maxTake=30;  break;
    case CHART_YEAR:  buckets=g_monthlyBuckets;count=g_monthlyCount;maxTake=12;  break;
    }
    int take=count<maxTake?count:maxTake;
    int start=count-take; if (start<0){start=0;take=count;}
    AggBucket *plot=(AggBucket*)HeapAlloc(GetProcessHeap(),0,take*sizeof(AggBucket));
    if (plot) memcpy(plot,buckets+start,take*sizeof(AggBucket));
    LeaveCriticalSection(&g_csData);
    if (!plot||take<2){if(plot)HeapFree(GetProcessHeap(),0,plot);return;}

    /* 85% threshold line */
    HPEN hThrPen=CreatePen(PS_DASH,1,CLR_ORANGE);
    HPEN hOld=(HPEN)SelectObject(hdc,hThrPen);
    int thrY=pt+ph-(ph*85/100);
    MoveToEx(hdc,pl,thrY,NULL); LineTo(hdc,pl+pw,thrY);
    SelectObject(hdc,hOld); DeleteObject(hThrPen);
    SetTextColor(hdc,CLR_ORANGE); SetBkMode(hdc,TRANSPARENT);
    RECT thrR={pl+pw-40,thrY-16,pl+pw-2,thrY-2};
    DrawTextA(hdc,"85%",-1,&thrR,DT_RIGHT|DT_BOTTOM|DT_SINGLELINE);

    /* Grid */
    HPEN hGd=CreatePen(PS_SOLID,1,CLR_BORDER);
    hOld=(HPEN)SelectObject(hdc,hGd);
    int g;
    for (g=0;g<=100;g+=25) {
        int y=pt+ph-(ph*g/100);
        MoveToEx(hdc,pl,y,NULL); LineTo(hdc,pl+pw,y);
        char lb[8]; snprintf(lb,sizeof(lb),"%d%%",g);
        SetTextColor(hdc,CLR_MUTED);
        RECT tr={2,y-8,pl-4,y+8};
        DrawTextA(hdc,lb,-1,&tr,DT_RIGHT|DT_VCENTER|DT_SINGLELINE);
    }
    /* bottom axis */
    MoveToEx(hdc,pl,pt+ph,NULL); LineTo(hdc,pl+pw,pt+ph);
    SelectObject(hdc,hOld); DeleteObject(hGd);

    /* ---- Gradient fill under Page File line ---- */
    {
        POINT *pts=(POINT*)HeapAlloc(GetProcessHeap(),0,(take+2)*sizeof(POINT));
        int i;
        for (i=0;i<take;i++) {
            double v=plot[i].sampleCount>0?plot[i].pfSum/plot[i].sampleCount:0;
            pts[i].x=pl+(int)((double)i/(take-1)*pw);
            int y=pt+ph-(int)(v/100.0*ph);
            if (y<pt)y=pt;
            if (y>pt+ph)y=pt+ph;
            pts[i].y=y;
        }
        pts[take].x=pts[take-1].x; pts[take].y=pt+ph;
        pts[take+1].x=pts[0].x; pts[take+1].y=pt+ph;
        /* gradient fill: top=accent at 15% alpha, bottom=transparent */
        for (i=pl;i<pl+pw;i++) {
            int seg=-1;
            int s;
            for (s=0;s<take-1;s++) if (i>=pts[s].x&&i<=pts[s+1].x){seg=s;break;}
            if (seg<0) continue;
            double t=(double)(i-pts[seg].x)/(pts[seg+1].x-pts[seg].x);
            int lineY=(int)(pts[seg].y+t*(pts[seg+1].y-pts[seg].y));
            if (lineY<pt+ph) {
                /* alpha blend: fade 0→40 intensity toward bottom */
                int hh=(pt+ph)-lineY;
                int alpha=hh<ph/2?30:30-20*(hh-ph/2)/(ph/2);
                if (alpha<5) alpha=5;
                COLORREF fc=CLR_CHART_PF;
                int r=GetRValue(fc),gr=GetGValue(fc),b=GetBValue(fc);
                int br=GetRValue(CLR_CARD),bgr=GetGValue(CLR_CARD),bb=GetBValue(CLR_CARD);
                r=r*alpha/100+br*(100-alpha)/100;
                gr=gr*alpha/100+bgr*(100-alpha)/100;
                b=b*alpha/100+bb*(100-alpha)/100;
                HPEN hPg=CreatePen(PS_SOLID,1,RGB(r,gr,b));
                HPEN hPold=(HPEN)SelectObject(hdc,hPg);
                MoveToEx(hdc,i,lineY,NULL); LineTo(hdc,i,pt+ph);
                SelectObject(hdc,hPold); DeleteObject(hPg);
            }
        }
        HeapFree(GetProcessHeap(),0,pts);
    }

    /* Physical Memory gradient (green) */
    {
        int i;
        for (i=pl;i<pl+pw;i++) {
            int seg=-1;
            int s;
            double phValPrev=plot[0].sampleCount>0?plot[0].phSum/plot[0].sampleCount:0;
            double phValNext=phValPrev;
            for (s=0;s<take-1;s++) {
                int x0=pl+(int)((double)s/(take-1)*pw);
                int x1=pl+(int)((double)(s+1)/(take-1)*pw);
                if (i>=x0&&i<=x1){seg=s;phValPrev=plot[s].sampleCount>0?plot[s].phSum/plot[s].sampleCount:0;phValNext=plot[s+1].sampleCount>0?plot[s+1].phSum/plot[s+1].sampleCount:0;break;}
            }
            if (seg<0) continue;
            double t=(double)(i-pl-(int)((double)seg/(take-1)*pw));
            double denom=(pl+(int)((double)(seg+1)/(take-1)*pw)-pl-(int)((double)seg/(take-1)*pw));
            if (denom==0) denom=1;
            t/=denom;
            double v=phValPrev+t*(phValNext-phValPrev);
            int lineY=pt+ph-(int)(v/100.0*ph);
            if (lineY<pt)lineY=pt;
            if (lineY>pt+ph)lineY=pt+ph;
            if (lineY<pt+ph) {
                int hh=(pt+ph)-lineY,alpha=hh<ph/2?25:25-15*(hh-ph/2)/(ph/2);
                if (alpha<3)alpha=3;
                COLORREF fc=CLR_CHART_PH;
                int r=GetRValue(fc),gr=GetGValue(fc),b=GetBValue(fc);
                r=r*alpha/100+GetRValue(CLR_CARD)*(100-alpha)/100;
                gr=gr*alpha/100+GetGValue(CLR_CARD)*(100-alpha)/100;
                b=b*alpha/100+GetBValue(CLR_CARD)*(100-alpha)/100;
                HPEN hPg=CreatePen(PS_SOLID,1,RGB(r,gr,b));
                HPEN hPold=(HPEN)SelectObject(hdc,hPg);
                MoveToEx(hdc,i,lineY,NULL); LineTo(hdc,i,pt+ph);
                SelectObject(hdc,hPold); DeleteObject(hPg);
            }
        }
    }

    /* Page File line (solid blue, 3px) */
    HPEN hPf=CreatePen(PS_SOLID,3,CLR_CHART_PF);
    hOld=(HPEN)SelectObject(hdc,hPf);
    int i;
    for (i=0;i<take;i++) {
        double v=plot[i].sampleCount>0?plot[i].pfSum/plot[i].sampleCount:0;
        int x=pl+(int)((double)i/(take-1)*pw);
        int y=pt+ph-(int)(v/100.0*ph);
        if (y<pt)y=pt; if (y>pt+ph)y=pt+ph;
        if (i==0) MoveToEx(hdc,x,y,NULL); else LineTo(hdc,x,y);
    }
    SelectObject(hdc,hOld); DeleteObject(hPf);

    /* Physical Memory line (dashed green, 2px) */
    HPEN hPh=CreatePen(PS_DASH,2,CLR_CHART_PH);
    hOld=(HPEN)SelectObject(hdc,hPh);
    for (i=0;i<take;i++) {
        double v=plot[i].sampleCount>0?plot[i].phSum/plot[i].sampleCount:0;
        int x=pl+(int)((double)i/(take-1)*pw);
        int y=pt+ph-(int)(v/100.0*ph);
        if (y<pt)y=pt; if (y>pt+ph)y=pt+ph;
        if (i==0) MoveToEx(hdc,x,y,NULL); else LineTo(hdc,x,y);
    }
    SelectObject(hdc,hOld); DeleteObject(hPh);

    /* Dot markers on the PF line (every N points) */
    int dotStep=take>12?take/6:1;
    for (i=0;i<take;i+=dotStep) {
        double v=plot[i].sampleCount>0?plot[i].pfMax:0;
        int x=pl+(int)((double)i/(take-1)*pw);
        int y=pt+ph-(int)(v/100.0*ph);
        if (y<pt)y=pt; if (y>pt+ph)y=pt+ph;
        HBRUSH hDot=CreateSolidBrush(CLR_CHART_PF);
        HPEN hDotP=CreatePen(PS_SOLID,2,CLR_CARD);
        SelectObject(hdc,hDot); SelectObject(hdc,hDotP);
        Ellipse(hdc,x-4,y-4,x+4,y+4);
        SelectObject(hdc,hOld); DeleteObject(hDot); DeleteObject(hDotP);
        /* max value label */
        char mx[8]; snprintf(mx,sizeof(mx),"%.0f%%",v);
        SetTextColor(hdc,CLR_CHART_PF);
        RECT mr={x-18,y-22,x+18,y-8};
        DrawTextA(hdc,mx,-1,&mr,DT_CENTER|DT_BOTTOM|DT_SINGLELINE);
    }

    /* X-axis labels */
    SetTextColor(hdc,CLR_MUTED);
    for (i=0;i<take;i++) {
        if (take>12&&i%(take/6)!=0&&i!=take-1) continue;
        int x=pl+(int)((double)i/(take-1)*pw);
        char lb[32]; struct tm *tm=localtime(&plot[i].bucketStart);
        if (chartRange==CHART_DAY) strftime(lb,sizeof(lb),"%H:%M",tm);
        else if (chartRange==CHART_YEAR) strftime(lb,sizeof(lb),"%b",tm);
        else strftime(lb,sizeof(lb),"%m/%d",tm);
        RECT tr={x-25,pt+ph+6,x+25,pt+ph+22};
        DrawTextA(hdc,lb,-1,&tr,DT_CENTER|DT_TOP|DT_SINGLELINE);
    }

    /* Legend — rounded pill style */
    {
        RECT lg={pl,8,pl+200,26};
        HBRUSH hLg=CreateSolidBrush(CLR_CARD2);
        HPEN hLgP=CreatePen(PS_SOLID,1,CLR_BORDER_LT);
        SelectObject(hdc,hLg); SelectObject(hdc,hLgP);
        RoundRect(hdc,lg.left,lg.top,lg.right,lg.bottom,8,8);
        SelectObject(hdc,hOld); DeleteObject(hLg); DeleteObject(hLgP);

        HBRUSH hPfBr=CreateSolidBrush(CLR_CHART_PF);
        SelectObject(hdc,hPfBr);
        RECT sw1={pl+8,13,pl+20,21};
        FillRect(hdc,&sw1,hPfBr);
        DeleteObject(hPfBr);
        SetTextColor(hdc,CLR_TEXT);
        RECT lt1={pl+24,11,pl+70,23};
        DrawTextA(hdc,L10N(K_CHART_LEGEND_PF),-1,&lt1,DT_LEFT|DT_VCENTER|DT_SINGLELINE);

        HBRUSH hPhBr=CreateSolidBrush(CLR_CHART_PH);
        SelectObject(hdc,hPhBr);
        RECT sw2={pl+78,13,pl+90,21};
        FillRect(hdc,&sw2,hPhBr);
        DeleteObject(hPhBr);
        RECT lt2={pl+94,11,pl+160,23};
        DrawTextA(hdc,L10N(K_CHART_LEGEND_PH),-1,&lt2,DT_LEFT|DT_VCENTER|DT_SINGLELINE);

        HBRUSH hThBr=CreateSolidBrush(CLR_ORANGE);
        SelectObject(hdc,hThBr);
        RECT sw3={pl+160,13,pl+172,21};
        FillRect(hdc,&sw3,hThBr);
        DeleteObject(hThBr);
        RECT lt3={pl+176,11,pl+220,23};
        DrawTextA(hdc,L10N(K_CHART_LEGEND_THR),-1,&lt3,DT_LEFT|DT_VCENTER|DT_SINGLELINE);

        SelectObject(hdc,hOld);
    }

    HeapFree(GetProcessHeap(),0,plot);
}

/* ============================================================================
 * Overview panel — GDI-drawn summary cards at top of Overview tab
 * ============================================================================ */
static void DrawOverviewPanel(HDC hdc, RECT rc) {
    int w=rc.right-rc.left, h2=rc.bottom-rc.top;
    if (w<=0||h2<=0) return;

    /* Card background */
    HBRUSH hBg=CreateSolidBrush(CLR_CARD);
    HPEN hBorder=CreatePen(PS_SOLID,1,CLR_BORDER);
    SelectObject(hdc,hBg); SelectObject(hdc,hBorder);
    Rectangle(hdc,rc.left,rc.top,rc.right,rc.bottom);
    DeleteObject(hBg); DeleteObject(hBorder);

    /* Title */
    SetTextColor(hdc,CLR_TEXT);
    SetBkMode(hdc,TRANSPARENT);
    if (g_hTitleFont) SelectObject(hdc,g_hTitleFont);
    RECT tr={rc.left+12,rc.top+8,rc.right-12,rc.top+30};
    DrawTextA(hdc,L10N(K_CARD_TITLE),-1,&tr,DT_LEFT|DT_VCENTER|DT_SINGLELINE);

    /* 4 metric cards in a row */
    int cardW=(w-48)/4;
    int cardH=h2-44;
    int c, cx=rc.left+12, cy=rc.top+36;

    DWORD pf=g_latestSnapshot.pageFilePct;
    DWORD ph=g_latestSnapshot.physLoad;
    DWORD idle=g_latestSnapshot.idleSeconds;
    time_t uptime=time(NULL)-g_tStartTime;

    COLORREF pfClr=pf>85?CLR_RED:pf>60?CLR_YELLOW:CLR_GREEN;
    COLORREF phClr=ph>90?CLR_RED:ph>70?CLR_YELLOW:CLR_GREEN;

    for (c=0;c<4;c++) {
        RECT card={cx,cy,cx+cardW,cy+cardH};
        HBRUSH hCb=CreateSolidBrush(CLR_CARD2);
        HPEN hCp=CreatePen(PS_SOLID,1,CLR_BORDER_LT);
        SelectObject(hdc,hCb); SelectObject(hdc,hCp);
        RoundRect(hdc,card.left,card.top,card.right,card.bottom,6,6);
        DeleteObject(hCb); DeleteObject(hCp);

        const char *label; char value[32], sub[64];
        COLORREF valClr=CLR_TEXT;

        switch (c) {
        case 0:
            label=L10N(K_CARD_PF_LABEL);
            snprintf(value,sizeof(value),"%lu%%",pf); valClr=pfClr;
            snprintf(sub,sizeof(sub),L10N(K_CARD_PF_SUB),PAGE_FILE_THRESHOLD_PCT);
            break;
        case 1:
            label=L10N(K_CARD_PH_LABEL);
            snprintf(value,sizeof(value),"%lu%%",ph); valClr=phClr;
            {
                char t[16],a[16];
                FmtMB(t,sizeof(t),g_latestSnapshot.totalPhys);
                FmtMB(a,sizeof(a),g_latestSnapshot.availPhys);
                snprintf(sub,sizeof(sub),L10N(K_CARD_PH_SUB),a,t);
            }
            break;
        case 2:
            label=L10N(K_CARD_IDLE_LABEL);
            FmtDur(value,sizeof(value),idle); valClr=CLR_ACCENT;
            snprintf(sub,sizeof(sub),L10N(K_CARD_IDLE_SUB),IDLE_THRESHOLD_SEC/60);
            break;
        case 3:
            label=L10N(K_CARD_UPTIME_LABEL);
            FmtDur(value,sizeof(value),uptime); valClr=CLR_GREEN;
            snprintf(sub,sizeof(sub),L10N(K_CARD_UPTIME_SUB),g_httpPort,DB_FILE_NAME);
            break;
        }

        SetTextColor(hdc,CLR_MUTED);
        if (g_hGuiFont) SelectObject(hdc,g_hGuiFont);
        RECT lb={card.left+10,card.top+6,card.right-10,card.top+22};
        DrawTextA(hdc,label,-1,&lb,DT_LEFT|DT_VCENTER|DT_SINGLELINE);

        SetTextColor(hdc,valClr);
        if (g_hTitleFont) SelectObject(hdc,g_hTitleFont);
        RECT vr={card.left+10,card.top+26,card.right-10,card.top+52};
        DrawTextA(hdc,value,-1,&vr,DT_LEFT|DT_VCENTER|DT_SINGLELINE);

        SetTextColor(hdc,CLR_MUTED);
        if (g_hGuiFont) SelectObject(hdc,g_hGuiFont);
        RECT sr={card.left+10,card.top+cardH-22,card.right-10,card.top+cardH-6};
        DrawTextA(hdc,sub,-1,&sr,DT_LEFT|DT_VCENTER|DT_SINGLELINE);

        cx+=cardW+8;
    }

    /* Section divider text */
    SetTextColor(hdc,CLR_MUTED);
    if (g_hGuiFont) SelectObject(hdc,g_hGuiFont);
    {
        RECT secR={rc.left+12,rc.top+h2+4,rc.right-12,rc.top+h2+22};
        (void)secR; /* placeholder for future section divider */
    }
}

/* ============================================================================
 * Overview pane — custom window hosting the overview cards + process + action
 * ============================================================================ */
static LRESULT CALLBACK OverviewProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC hdc=BeginPaint(hwnd,&ps);
        RECT rc; GetClientRect(hwnd,&rc);
        /* top part: overview cards (96px) */
        RECT top={4,4,rc.right-4,88};
        DrawOverviewPanel(hdc,top);
        EndPaint(hwnd,&ps);
        return 0;
    }
    case WM_ERASEBKGND: return 1;
    }
    return DefWindowProcA(hwnd,msg,wParam,lParam);
}

/* ============================================================================
 * Chart sub-window
 * ============================================================================ */
static LRESULT CALLBACK ChartProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC hdc=BeginPaint(hwnd,&ps);
        RECT rc; GetClientRect(hwnd,&rc);
        DrawGdiChart(hdc,rc,g_chartRange);
        EndPaint(hwnd,&ps);
        return 0;
    }
    case WM_ERASEBKGND: return 1;
    }
    return DefWindowProcA(hwnd,msg,wParam,lParam);
}

/* ============================================================================
 * Custom-draw ListView — dark rows with severity-colored left margin
 * ============================================================================ */
static void UpdateProcessList(void) {
    if (!g_hProcList) return;
    LvDelAll(g_hProcList);
    EnterCriticalSection(&g_csData);
    MemorySnapshot *s=&g_latestSnapshot;
    int i;
    for (i=0;i<s->numProcesses;i++) {
        ProcessInfo *p=&s->topProcesses[i];
        char rank[8],pid[12],commit[32],ws[32],growth[32];
        snprintf(rank,sizeof(rank),"%d",i+1);
        snprintf(pid,sizeof(pid),"%lu",p->pid);
        FmtMB(commit,sizeof(commit),p->commitSize);
        FmtMB(ws,sizeof(ws),p->workingSet);
        if (p->growthRateMBps>0.01)
            snprintf(growth,sizeof(growth),"+%.1f MB/s",p->growthRateMBps);
        else strcpy(growth,"-");

        LVITEMA it; memset(&it,0,sizeof(it));
        it.mask=LVIF_TEXT; it.iItem=i; it.pszText=rank;
        LvInsertItem(g_hProcList,&it);
        LvSetText(g_hProcList,i,1,pid);
        LvSetText(g_hProcList,i,2,p->name);
        LvSetText(g_hProcList,i,3,commit);
        LvSetText(g_hProcList,i,4,ws);
        LvSetText(g_hProcList,i,5,growth);
    }
    LeaveCriticalSection(&g_csData);
}

static void UpdateAnomalyList(void) {
    if (!g_hAnomalyList) return;
    LvDelAll(g_hAnomalyList);
    EnterCriticalSection(&g_csData);
    int i;
    for (i=0;i<g_anomalyCount;i++) {
        AnomalyAlert *a=&g_anomalies[i];
        char timeStr[32],typeStr[16],pidStr[12],valueStr[32];
        FmtTime(timeStr,sizeof(timeStr),a->timestamp);
        switch (a->type) {
        case ANOMALY_CPU_HOG:    strcpy(typeStr,"CPU Hog"); break;
        case ANOMALY_MEM_HOG:    strcpy(typeStr,"Mem Hog"); break;
        case ANOMALY_MEM_LEAK:   strcpy(typeStr,"Mem Leak"); break;
        case ANOMALY_GPU_HOG:    strcpy(typeStr,"GPU Hog"); break;
        case ANOMALY_SUSPICIOUS: strcpy(typeStr,"Suspicious"); break;
        default:                 strcpy(typeStr,"Unknown"); break;
        }
        snprintf(pidStr,sizeof(pidStr),"%lu",a->pid);
        snprintf(valueStr,sizeof(valueStr),"%.1f",a->value);

        LVITEMA it; memset(&it,0,sizeof(it));
        it.mask=LVIF_TEXT; it.iItem=i; it.pszText=timeStr;
        LvInsertItem(g_hAnomalyList,&it);
        LvSetText(g_hAnomalyList,i,1,typeStr);
        LvSetText(g_hAnomalyList,i,2,pidStr);
        LvSetText(g_hAnomalyList,i,3,a->procName);
        LvSetText(g_hAnomalyList,i,4,valueStr);
        LvSetText(g_hAnomalyList,i,5,a->description);
    }
    LeaveCriticalSection(&g_csData);
}

static void UpdateSuspiciousList(void) {
    if (!g_hSuspList) return;
    LvDelAll(g_hSuspList);
    EnterCriticalSection(&g_csData);
    int i;
    for (i=0;i<g_suspProcCount;i++) {
        SuspiciousProc *sp=&g_suspProcs[i];
        char pidStr[12],firstMB[32],lastMB[32],growthMB[32];
        char rateStr[32],firstSeen[32],lastSeen[32],alerts[8];
        snprintf(pidStr,sizeof(pidStr),"%lu",sp->pid);
        FmtMB(firstMB,sizeof(firstMB),sp->firstCommit);
        FmtMB(lastMB,sizeof(lastMB),sp->lastCommit);
        SIZE_T gb=sp->lastCommit>sp->firstCommit?(sp->lastCommit-sp->firstCommit):0;
        FmtMB(growthMB,sizeof(growthMB),gb);
        snprintf(rateStr,sizeof(rateStr),"%.1f MB/s",sp->peakGrowthRate);
        FmtTime(firstSeen,sizeof(firstSeen),sp->firstSeen);
        FmtTime(lastSeen,sizeof(lastSeen),sp->lastSeen);
        snprintf(alerts,sizeof(alerts),"%d",sp->alertCount);

        LVITEMA it; memset(&it,0,sizeof(it));
        it.mask=LVIF_TEXT; it.iItem=i; it.pszText=pidStr;
        LvInsertItem(g_hSuspList,&it);
        LvSetText(g_hSuspList,i,1,sp->name);
        LvSetText(g_hSuspList,i,2,firstMB);
        LvSetText(g_hSuspList,i,3,lastMB);
        LvSetText(g_hSuspList,i,4,growthMB);
        LvSetText(g_hSuspList,i,5,rateStr);
        LvSetText(g_hSuspList,i,6,firstSeen);
        LvSetText(g_hSuspList,i,7,lastSeen);
        LvSetText(g_hSuspList,i,8,alerts);
    }
    LeaveCriticalSection(&g_csData);
}

static void UpdateActionList(void) {
    if (!g_hActionList) return;
    LvDelAll(g_hActionList);
    EnterCriticalSection(&g_csData);
    int i;
    for (i=0;i<g_actionCount;i++) {
        ActionRecord *a=&g_actions[i];
        char timeStr[32],bfStr[16],afStr[16],tcStr[8],fcStr[8],desc[512];
        FmtTime(timeStr,sizeof(timeStr),a->timestamp);
        /* Show delta */
        int delta=(int)a->pageFileBefore-(int)a->pageFileAfter;
        snprintf(bfStr,sizeof(bfStr),"%lu%%",a->pageFileBefore);
        snprintf(afStr,sizeof(afStr),"%lu%% %s",a->pageFileAfter,delta>0?"":delta<0?"":">");
        snprintf(tcStr,sizeof(tcStr),"%d",a->trimmedCount);
        snprintf(fcStr,sizeof(fcStr),"%d",a->failedCount);
        snprintf(desc,sizeof(desc),"%s [%s%d%%]",
            a->description,delta>0?"-":"+",abs(delta));

        LVITEMA it; memset(&it,0,sizeof(it));
        it.mask=LVIF_TEXT; it.iItem=i; it.pszText=timeStr;
        LvInsertItem(g_hActionList,&it);
        LvSetText(g_hActionList,i,1,bfStr);
        LvSetText(g_hActionList,i,2,afStr);
        LvSetText(g_hActionList,i,3,tcStr);
        LvSetText(g_hActionList,i,4,fcStr);
        LvSetText(g_hActionList,i,5,desc);
    }
    LeaveCriticalSection(&g_csData);
}

static void RefreshDesktopData(HWND hwnd) {
    (void)hwnd;
    UpdateProcessList();
    UpdateAnomalyList();
    UpdateSuspiciousList();
    UpdateActionList();
    if (g_hOverviewPane) InvalidateRect(g_hOverviewPane,NULL,TRUE);
    if (g_hChartArea)    InvalidateRect(g_hChartArea,NULL,TRUE);
}

/* ============================================================================
 * Tab management
 * ============================================================================ */
typedef enum { TAB_OVERVIEW=0, TAB_PROCESSES, TAB_CHART, TAB_ANOMALIES, TAB_SUSPICIOUS } TabIdx;

static void ShowTab(TabIdx tab) {
    int ov=(tab==TAB_OVERVIEW),pr=(tab==TAB_PROCESSES),ch=(tab==TAB_CHART);
    int an=(tab==TAB_ANOMALIES),su=(tab==TAB_SUSPICIOUS);

    ShowWindow(g_hOverviewPane,ov?SW_SHOW:SW_HIDE);
    ShowWindow(g_hProcList,(ov||pr)?SW_SHOW:SW_HIDE);
    ShowWindow(g_hActionList,ov?SW_SHOW:SW_HIDE);
    ShowWindow(g_hChartArea,ch?SW_SHOW:SW_HIDE);
    ShowWindow(g_hComboRange,ch?SW_SHOW:SW_HIDE);
    ShowWindow(g_hLblChartRange,ch?SW_SHOW:SW_HIDE);
    ShowWindow(g_hAnomalyList,an?SW_SHOW:SW_HIDE);
    ShowWindow(g_hSuspList,su?SW_SHOW:SW_HIDE);

    if (ch) InvalidateRect(g_hChartArea,NULL,TRUE);
    if (ov) InvalidateRect(g_hOverviewPane,NULL,TRUE);
}

/* ============================================================================
 * ListView creation helper
 * ============================================================================ */
static HWND CreateLV(HWND parent, int x, int y, int w, int h,
                     const char **hdrs, int nCols, int *widths) {
    HWND lv=CreateWindowExA(0,WC_LISTVIEWA,NULL,
        WS_CHILD|WS_VISIBLE|LVS_REPORT|LVS_SINGLESEL|LVS_SHOWSELALWAYS,
        x,y,w,h,parent,NULL,GetModuleHandleA(NULL),NULL);
    LvSetExSt(lv,LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER|LVS_EX_GRIDLINES);

    LV_COLUMNA col; int i;
    for (i=0;i<nCols;i++) {
        memset(&col,0,sizeof(col));
        col.mask=LVCF_TEXT|LVCF_WIDTH|LVCF_FMT;
        col.fmt=LVCFMT_LEFT; col.pszText=(LPSTR)hdrs[i]; col.cx=widths[i];
        LvInsertCol(lv,i,&col);
    }
    /* Dark background for listview */
    ListView_SetBkColor(lv,CLR_CARD);
    ListView_SetTextBkColor(lv,CLR_CARD);
    ListView_SetTextColor(lv,CLR_TEXT2);
    return lv;
}

/* ============================================================================
 * Status bar update
 * ============================================================================ */
static void UpdateStatusBar(HWND hwnd) {
    (void)hwnd;
    if (!g_hStatusBar) return;
    char txt[512];
    DWORD pf=g_latestSnapshot.pageFilePct,ph=g_latestSnapshot.physLoad;
    DWORD idle=g_latestSnapshot.idleSeconds;
    snprintf(txt,sizeof(txt),
        L10N(K_STATUS_FMT),
        pf,ph,idle,g_httpPort);
    int bl=(int)strlen(txt);
    FmtDur(txt+bl,sizeof(txt)-bl,time(NULL)-g_tStartTime);
    SendMessageA(g_hStatusBar,SB_SETTEXTA,0,(LPARAM)txt);
}

/* ============================================================================
 * Main window procedure
 * ============================================================================ */
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        InitCommonControls();
        g_hDesktopWnd=hwnd;
        EnableDarkTitleBar(hwnd);

        /* Fonts — use CJK-capable typeface so Chinese renders correctly.
         * On zh-CN/zh-TW Windows, "Microsoft YaHei" / "Microsoft JhengHei" are
         * always present and support the full CJK range. Segoe UI is secondary
         * for Western glyphs, but is buggy with Chinese on some Win10 builds. */
        {
            LocaleId loc = LocaleGet();
            const char *uiFace = (loc == LOC_ZH_TW) ?
                "Microsoft JhengHei" : "Microsoft YaHei";
            const char *titleFace = (loc == LOC_ZH_TW) ?
                "Microsoft JhengHei" : "Microsoft YaHei";

            g_hGuiFont=CreateFontA(15,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,
                DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY,FF_DONTCARE,uiFace);
            g_hTitleFont=CreateFontA(18,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,
                DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY,FF_DONTCARE,titleFace);
            g_hMonoFont=CreateFontA(13,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,
                DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY,FF_DONTCARE,"Consolas");
        }

        RECT rc; GetClientRect(hwnd,&rc);
        int cw=rc.right-rc.left,ch=rc.bottom-rc.top;

        /* ---- Top bar: tabs + buttons ---- */
        g_hTab=CreateWindowExA(0,WC_TABCONTROLA,NULL,
            WS_CHILD|WS_VISIBLE|TCS_FIXEDWIDTH,
            8,6,cw-16,28,hwnd,(HMENU)IDC_TAB,
            GetModuleHandleA(NULL),NULL);
        SendMessageA(g_hTab,WM_SETFONT,(WPARAM)g_hGuiFont,TRUE);
        {
            TCITEMA tci; memset(&tci,0,sizeof(tci)); tci.mask=TCIF_TEXT;
            tci.pszText=L10N(K_TAB_OVERVIEW);  TcInsItem(g_hTab,TAB_OVERVIEW,&tci);
            tci.pszText=L10N(K_TAB_PROCESSES); TcInsItem(g_hTab,TAB_PROCESSES,&tci);
            tci.pszText=L10N(K_TAB_CHARTS);    TcInsItem(g_hTab,TAB_CHART,&tci);
            tci.pszText=L10N(K_TAB_ANOMALIES);  TcInsItem(g_hTab,TAB_ANOMALIES,&tci);
            tci.pszText=L10N(K_TAB_SUSPICIOUS); TcInsItem(g_hTab,TAB_SUSPICIOUS,&tci);
        }

        /* Top-right buttons */
        g_hBtnCleanup=CreateWindowExA(0,"BUTTON",L10N(K_BTN_CLEANUP),
            WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON|BS_FLAT,
            cw-340,8,100,26,hwnd,(HMENU)IDC_BTN_CLEANUP,
            GetModuleHandleA(NULL),NULL);
        SendMessageA(g_hBtnCleanup,WM_SETFONT,(WPARAM)g_hGuiFont,TRUE);

        HWND btnExit=CreateWindowExA(0,"BUTTON",L10N(K_BTN_EXIT),
            WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON|BS_FLAT,
            cw-120,8,100,26,hwnd,(HMENU)IDC_BTN_EXIT,
            GetModuleHandleA(NULL),NULL);
        SendMessageA(btnExit,WM_SETFONT,(WPARAM)g_hGuiFont,TRUE);

        /* Chart controls (initially hidden) */
        g_hLblChartRange=CreateWindowExA(0,"STATIC",L10N(K_CHART_RANGE_LBL),
            WS_CHILD|SS_LEFT,16,42,80,20,hwnd,NULL,
            GetModuleHandleA(NULL),NULL);
        SendMessageA(g_hLblChartRange,WM_SETFONT,(WPARAM)g_hGuiFont,TRUE);
        ShowWindow(g_hLblChartRange,SW_HIDE);

        g_hComboRange=CreateWindowExA(0,"COMBOBOX",NULL,
            WS_CHILD|CBS_DROPDOWNLIST|WS_VSCROLL,
            100,40,140,200,hwnd,(HMENU)IDC_COMBO_RANGE,
            GetModuleHandleA(NULL),NULL);
        SendMessageA(g_hComboRange,WM_SETFONT,(WPARAM)g_hGuiFont,TRUE);
        SendMessageA(g_hComboRange,CB_ADDSTRING,0,(LPARAM)L10N(K_CHART_RANGE_DAY));
        SendMessageA(g_hComboRange,CB_ADDSTRING,0,(LPARAM)L10N(K_CHART_RANGE_WEEK));
        SendMessageA(g_hComboRange,CB_ADDSTRING,0,(LPARAM)L10N(K_CHART_RANGE_MONTH));
        SendMessageA(g_hComboRange,CB_ADDSTRING,0,(LPARAM)L10N(K_CHART_RANGE_YEAR));
        SendMessageA(g_hComboRange,CB_SETCURSEL,CHART_WEEK,0);
        ShowWindow(g_hComboRange,SW_HIDE);

        /* Content area — starts below tab bar */
        int ct=44, cl=8, cright=cw-16;

        /* Overview pane (custom drawn cards) */
        {
            WNDCLASSA ocls; memset(&ocls,0,sizeof(ocls));
            ocls.lpfnWndProc=OverviewProc;
            ocls.hInstance=GetModuleHandleA(NULL);
            ocls.hCursor=LoadCursor(NULL,IDC_ARROW);
            ocls.hbrBackground=CreateSolidBrush(CLR_BG);
            ocls.lpszClassName="VMOverviewPane";
            RegisterClassA(&ocls);
            g_hOverviewPane=CreateWindowExA(0,"VMOverviewPane",NULL,
                WS_CHILD|WS_VISIBLE,
                cl,ct,cright,88,hwnd,NULL,GetModuleHandleA(NULL),NULL);
        }

        /* Process list — used in Overview + Processes tabs */
        {
            const char *hdr[]={"#","PID","Name","Commit","WS","Growth"};
            int wd[]={36,70,180,110,110,100};
            g_hProcList=CreateLV(hwnd,cl,ct+96,cright,ch-ct-350,hdr,6,wd);
        }

        /* Action log — Overview only, pinned bottom */
        {
            const char *hdr[]={"Time","Before","After","Trimmed","Failed","Description"};
            int wd[]={130,60,70,60,50,260};
            g_hActionList=CreateLV(hwnd,cl,ch-242,cright,200,hdr,6,wd);
        }

        /* Chart area */
        {
            WNDCLASSA ccls; memset(&ccls,0,sizeof(ccls));
            ccls.lpfnWndProc=ChartProc;
            ccls.hInstance=GetModuleHandleA(NULL);
            ccls.hCursor=LoadCursor(NULL,IDC_ARROW);
            ccls.hbrBackground=CreateSolidBrush(CLR_CARD);
            ccls.lpszClassName="VMChartArea";
            RegisterClassA(&ccls);
            g_hChartArea=CreateWindowExA(0,"VMChartArea",NULL,
                WS_CHILD|WS_VISIBLE|WS_BORDER,
                cl,68,cright,ch-100,hwnd,(HMENU)IDC_CHART_AREA,
                GetModuleHandleA(NULL),NULL);
        }

        /* Anomaly list */
        {
            const char *hdr[]={"Time","Type","PID","Process","Value","Description"};
            int wd[]={130,90,55,130,65,300};
            g_hAnomalyList=CreateLV(hwnd,cl,ct,cright,ch-ct-28,hdr,6,wd);
        }

        /* Suspicious list */
        {
            const char *hdr[]={"PID","Name","First","Last","Growth","Peak Rate","First Seen","Last Seen","Alerts"};
            int wd[]={55,120,95,95,90,85,125,125,50};
            g_hSuspList=CreateLV(hwnd,cl,ct,cright,ch-ct-28,hdr,9,wd);
        }

        /* Status bar */
        g_hStatusBar=CreateWindowExA(0,STATUSCLASSNAMEA,NULL,
            WS_CHILD|WS_VISIBLE|SBARS_SIZEGRIP,
            0,0,0,0,hwnd,NULL,GetModuleHandleA(NULL),NULL);
        SendMessageA(g_hStatusBar,WM_SETFONT,(WPARAM)g_hGuiFont,TRUE);

        /* Done creating all controls */

        ShowTab(TAB_OVERVIEW);
        AddTrayIcon(hwnd);
        SetTimer(hwnd,IDT_DESKTOP_REFRESH,DESKTOP_REFRESH_MS,NULL);
        return 0;
    }

    case WM_NOTIFY: {
        NMHDR *nm=(NMHDR*)lParam;
        if (nm->idFrom==IDC_TAB && nm->code==TCN_SELCHANGE) {
            int sel=TcGetSel(g_hTab);
            switch (sel) {
            case 0: ShowTab(TAB_OVERVIEW); break;
            case 1: ShowTab(TAB_PROCESSES); break;
            case 2: ShowTab(TAB_CHART); break;
            case 3: ShowTab(TAB_ANOMALIES); break;
            case 4: ShowTab(TAB_SUSPICIOUS); break;
            }
            RefreshDesktopData(hwnd);
        }
        /* Custom draw for ListViews */
        if (nm->code==NM_CUSTOMDRAW) {
            NMLVCUSTOMDRAW *lcd=(NMLVCUSTOMDRAW*)lParam;
            if (nm->idFrom==GetDlgCtrlID(g_hAnomalyList)) {
                switch (lcd->nmcd.dwDrawStage) {
                case CDDS_PREPAINT: SetWindowLongPtrA(hwnd,DWLP_MSGRESULT,CDRF_NOTIFYITEMDRAW); return CDRF_NOTIFYITEMDRAW;
                case CDDS_ITEMPREPAINT: {
                    int idx=(int)lcd->nmcd.dwItemSpec;
                    lcd->clrTextBk=idx%2==0?CLR_CARD:CLR_CARD2;
                    lcd->clrText=CLR_TEXT2;
                    /* Red bar for severity */
                    AnomalyAlert *a2=NULL;
                    EnterCriticalSection(&g_csData);
                    if (idx<g_anomalyCount) a2=&g_anomalies[idx];
                    LeaveCriticalSection(&g_csData);
                    if (a2) {
                        COLORREF sev=CLR_RED;
                        if (a2->type==ANOMALY_MEM_HOG||a2->type==ANOMALY_GPU_HOG) sev=CLR_YELLOW;
                        RECT rc2; ListView_GetItemRect(nm->hwndFrom,idx,&rc2,LVIR_BOUNDS);
                        HDC hdc=lcd->nmcd.hdc;
                        HBRUSH hb=CreateSolidBrush(sev);
                        RECT bar={rc2.left,rc2.top,rc2.left+3,rc2.bottom};
                        FillRect(hdc,&bar,hb);
                        DeleteObject(hb);
                    }
                    SetWindowLongPtrA(hwnd,DWLP_MSGRESULT,CDRF_NEWFONT);
                    return CDRF_NEWFONT;
                }
                }
            }
            /* Other listviews: just alternating row colors */
            if (nm->idFrom==GetDlgCtrlID(g_hProcList)||
                nm->idFrom==GetDlgCtrlID(g_hSuspList)||
                nm->idFrom==GetDlgCtrlID(g_hActionList)) {
                switch (lcd->nmcd.dwDrawStage) {
                case CDDS_PREPAINT: SetWindowLongPtrA(hwnd,DWLP_MSGRESULT,CDRF_NOTIFYITEMDRAW); return CDRF_NOTIFYITEMDRAW;
                case CDDS_ITEMPREPAINT:
                    lcd->clrTextBk=((int)lcd->nmcd.dwItemSpec)%2==0?CLR_CARD:CLR_CARD2;
                    lcd->clrText=CLR_TEXT2;
                    SetWindowLongPtrA(hwnd,DWLP_MSGRESULT,CDRF_NEWFONT);
                    return CDRF_NEWFONT;
                }
            }
        }
        return 0;
    }

    case WM_COMMAND: {
        WORD id=LOWORD(wParam);
        switch (id) {
        case IDC_BTN_CLEANUP: CheckAndAct(); RefreshDesktopData(hwnd); break;
        case IDC_BTN_EXIT:    DestroyWindow(hwnd); break;
        case IDC_COMBO_RANGE:
            if (HIWORD(wParam)==CBN_SELCHANGE) {
                g_chartRange=(int)SendMessageA((HWND)lParam,CB_GETCURSEL,0,0);
                InvalidateRect(g_hChartArea,NULL,TRUE);
            }
            break;
        case IDM_SHOW:    ShowWindow(hwnd,SW_RESTORE); SetForegroundWindow(hwnd); break;
        case IDM_CLEANUP: CheckAndAct(); RefreshDesktopData(hwnd); break;
        case IDM_EXIT:    DestroyWindow(hwnd); break;
        }
        return 0;
    }

    case WM_TRAYICON:
        if (LOWORD(lParam)==WM_RBUTTONUP) ShowTrayMenu(hwnd);
        else if (LOWORD(lParam)==WM_LBUTTONDBLCLK) {
            ShowWindow(hwnd,SW_RESTORE); SetForegroundWindow(hwnd);
        }
        return 0;

    case WM_SYSCOMMAND:
        if ((wParam&0xFFF0)==SC_MINIMIZE) {
            ShowWindow(hwnd,SW_HIDE);
            ShowTrayBalloon(hwnd,"VM Manager",
                L10N(K_TRAY_BALLOON_MIN));
            return 0;
        }
        break;

    case WM_CLOSE:
        ShowWindow(hwnd,SW_HIDE);
        ShowTrayBalloon(hwnd,"VM Manager",
            L10N(K_TRAY_BALLOON_CLOSE));
        return 0;

    case WM_DESTROY:
        KillTimer(hwnd,IDT_DESKTOP_REFRESH);
        RemoveTrayIcon(hwnd);
        PostQuitMessage(0);
        return 0;

    case WM_TIMER:
        if (wParam==IDT_DESKTOP_REFRESH) {
            RefreshDesktopData(hwnd);
            UpdateStatusBar(hwnd);
        }
        return 0;

    case WM_CTLCOLORSTATIC: {
        HDC hdc=(HDC)wParam;
        SetTextColor(hdc,CLR_MUTED);
        SetBkColor(hdc,CLR_BG);
        return (LRESULT)CreateSolidBrush(CLR_BG);
    }
    case WM_CTLCOLORBTN: {
        HDC hdc=(HDC)wParam;
        SetTextColor(hdc,CLR_TEXT2);
        SetBkColor(hdc,CLR_CARD2);
        return (LRESULT)CreateSolidBrush(CLR_CARD2);
    }
    case WM_CTLCOLORLISTBOX: {
        HDC hdc=(HDC)wParam;
        SetTextColor(hdc,CLR_TEXT2);
        SetBkColor(hdc,CLR_CARD);
        return (LRESULT)CreateSolidBrush(CLR_CARD);
    }

    case WM_SIZE: {
        RECT rc; GetClientRect(hwnd,&rc);
        int cw=rc.right-rc.left,ch=rc.bottom-rc.top;
        int ct=44,cl=8,cright=cw-16;

        if (g_hTab)         SetWindowPos(g_hTab,NULL,8,6,cw-16,28,SWP_NOZORDER);
        if (g_hBtnCleanup)  SetWindowPos(g_hBtnCleanup,NULL,cw-340,8,100,26,SWP_NOZORDER);
        { HWND btn=GetDlgItem(hwnd,IDC_BTN_EXIT); if(btn)SetWindowPos(btn,NULL,cw-120,8,100,26,SWP_NOZORDER); }
        if (g_hOverviewPane)SetWindowPos(g_hOverviewPane,NULL,cl,ct,cright,88,SWP_NOZORDER);
        if (g_hProcList)    SetWindowPos(g_hProcList,NULL,cl,ct+96,cright,ch-ct-350,SWP_NOZORDER);
        if (g_hActionList)  SetWindowPos(g_hActionList,NULL,cl,ch-242,cright,200,SWP_NOZORDER);
        if (g_hChartArea)   SetWindowPos(g_hChartArea,NULL,cl,68,cright,ch-100,SWP_NOZORDER);
        if (g_hAnomalyList) SetWindowPos(g_hAnomalyList,NULL,cl,ct,cright,ch-ct-28,SWP_NOZORDER);
        if (g_hSuspList)    SetWindowPos(g_hSuspList,NULL,cl,ct,cright,ch-ct-28,SWP_NOZORDER);
        SendMessageA(g_hStatusBar,WM_SIZE,0,0);
        return 0;
    }
    }

    return DefWindowProcA(hwnd,msg,wParam,lParam);
}

/* ============================================================================
 * RunDesktop
 * ============================================================================ */
int RunDesktop(void) {
    g_bDesktop=TRUE;

    WNDCLASSA wc; memset(&wc,0,sizeof(wc));
    wc.lpfnWndProc=WndProc;
    wc.hInstance=GetModuleHandleA(NULL);
    wc.hCursor=LoadCursor(NULL,IDC_ARROW);
    wc.hbrBackground=CreateSolidBrush(CLR_BG);
    wc.lpszClassName="VMManagerDesktop";
    wc.hIcon=LoadIcon(NULL,IDI_INFORMATION);
    if (!RegisterClassA(&wc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS) return 1;

    /* Use CreateWindowExW so the title bar renders Unicode correctly.
     * The window class is still ANSI but the title accepts WCHAR. */
    HWND hwnd=CreateWindowExW(0,
        L"VMManagerDesktop",
        L10NW(K_WIN_TITLE_MAIN),
        WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,
        CW_USEDEFAULT,CW_USEDEFAULT,1080,760,
        NULL,NULL,GetModuleHandleA(NULL),NULL);
    if (!hwnd) return 1;

    ShowWindow(hwnd,SW_SHOW); UpdateWindow(hwnd);

    /* Start engine if needed */
    if (!g_hHttpThread) {
        g_hHttpThread=CreateThread(NULL,0,HttpServerThread,NULL,0,NULL);
        int wc2=0; while (g_httpPort==0&&wc2<30){Sleep(100);wc2++;}
    }

    CheckAndAct();

    MSG msg;
    while (GetMessageA(&msg,NULL,0,0)) { TranslateMessage(&msg); DispatchMessageA(&msg); }

    g_bRunning=FALSE;
    RemoveTrayIcon(hwnd);
    return 0;
}
