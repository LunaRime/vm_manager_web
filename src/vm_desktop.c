/**
 * vm_desktop.c — Desktop GUI v3.0
 *   Unicode window registration → clean CJK title bar
 *   ClearType fonts (ANTIALIASED_QUALITY)
 *   All strings localized via L10N/L10NW
 *   New: bar chart mode (toggle) alongside line chart
 *   UI polish: section labels, better spacing
 */
#include "vm_common.h"
#include "vm_locale.h"

/* ============================================================================
 * Dwm dark title bar (Win10 1809+)
 * ============================================================================ */
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
/* MinGW 6.3.0 may lack this font-quality constant */
#ifndef CLEARTYPE_NATURAL_QUALITY
#define CLEARTYPE_NATURAL_QUALITY 6
#endif
typedef HRESULT (WINAPI *PFN_DwmSetWindowAttribute)(HWND,DWORD,LPCVOID,DWORD);
static void EnableDarkTitleBar(HWND hwnd){
    HMODULE hDwm=LoadLibraryA("dwmapi.dll");
    if(hDwm){
        PFN_DwmSetWindowAttribute pfn=(PFN_DwmSetWindowAttribute)
            GetProcAddress(hDwm,"DwmSetWindowAttribute");
        if(pfn){BOOL d=TRUE;pfn(hwnd,DWMWA_USE_IMMERSIVE_DARK_MODE,&d,sizeof(d));}
    }
}

/* ============================================================================
 * Forward declarations
 * ============================================================================ */
static LRESULT CALLBACK WndProc(HWND,UINT,WPARAM,LPARAM);
static LRESULT CALLBACK ChartProc(HWND,UINT,WPARAM,LPARAM);
static LRESULT CALLBACK OverviewProc(HWND,UINT,WPARAM,LPARAM);
static void DrawGdiLineChart(HDC,RECT,int);
static void DrawGdiBarChart(HDC,RECT,int);
static void DrawOverviewPanel(HDC,RECT);
static void RefreshDesktopData(HWND);
static void AddTrayIcon(HWND);
static void RemoveTrayIcon(HWND);
static void ShowTrayMenu(HWND);
static void ShowTrayBalloon(HWND,const WCHAR *title,const WCHAR *msg);

/* ============================================================================
 * ListView / TabCtrl raw SendMessage wrappers (MinGW 6.3.0 compat)
 * ============================================================================ */
static int  LvIns(HWND lv,const LVITEMA *it){return (int)SendMessageA(lv,LVM_INSERTITEMA,0,(LPARAM)it);}
static int  LvInsCol(HWND lv,int i,const LV_COLUMNA *c){return (int)SendMessageA(lv,LVM_INSERTCOLUMNA,(WPARAM)i,(LPARAM)c);}
static void LvSetTxt(HWND lv,int i,int sub,const char *t){
    LVITEMA lvi;memset(&lvi,0,sizeof(lvi));lvi.iSubItem=sub;lvi.pszText=(LPSTR)t;
    SendMessageA(lv,LVM_SETITEMTEXTA,(WPARAM)i,(LPARAM)&lvi);
}
static void LvDelA(HWND lv){SendMessageA(lv,LVM_DELETEALLITEMS,0,0);}
static void LvExSt(HWND lv,DWORD ex){SendMessageA(lv,LVM_SETEXTENDEDLISTVIEWSTYLE,0,(LPARAM)ex);}
static int  TcIns(HWND tc,int i,const TCITEMA *tci){return (int)SendMessageA(tc,TCM_INSERTITEMA,(WPARAM)i,(LPARAM)tci);}
static int  TcSel(HWND tc){return (int)SendMessageA(tc,TCM_GETCURSEL,0,0);}

/* ============================================================================
 * Globals
 * ============================================================================ */
static HWND  g_hDsk=NULL,g_hTab=NULL,g_hChart=NULL,g_hOvPane=NULL;
static HWND  g_hProc=NULL,g_hAnom=NULL,g_hSusp=NULL,g_hAct=NULL;
static HWND  g_hClnup=NULL,g_hCbo=NULL,g_hSts=NULL,g_hLblCbo=NULL;
static HWND  g_hBtnBar=NULL;
static int   g_range=CHART_WEEK;
static int   g_chartMode=0; /* 0=line, 1=bar */
static HFONT g_hFnt=NULL,g_hTitleFnt=NULL,g_hMonoFnt=NULL;

/* ============================================================================
 * Dark theme colors
 * ============================================================================ */
#define CLR_BG       RGB(13,17,23)
#define CLR_CARD     RGB(22,27,34)
#define CLR_CARD2    RGB(30,35,42)
#define CLR_BORDER   RGB(48,54,61)
#define CLR_BORDERLT RGB(58,64,71)
#define CLR_TEXT     RGB(225,230,237)
#define CLR_TEXT2    RGB(201,209,217)
#define CLR_MUTED    RGB(139,148,158)
#define CLR_ACCENT   RGB(88,166,255)
#define CLR_GREEN    RGB(63,185,80)
#define CLR_RED      RGB(248,81,73)
#define CLR_ORANGE   RGB(255,159,50)
#define CLR_YELLOW   RGB(210,153,34)
#define CLR_PF       RGB(88,166,255)
#define CLR_PH       RGB(63,185,80)
#define CLR_BAR_BLUE RGB(88,166,255)
#define CLR_BAR_GREEN RGB(63,185,80)
#define CLR_BAR_RED   RGB(248,81,73)
#define CLR_BAR_YELLOW RGB(210,153,34)

/* ============================================================================
 * Formatting helpers
 * ============================================================================ */
static void FmtMB(char *o,int sz,ULONGLONG b){
    if(b>=1073741824ULL)snprintf(o,sz,"%.1f GB",b/1073741824.0);
    else snprintf(o,sz,"%I64u MB",(unsigned long long)(b/1048576));
}
static void FmtTime(char *o,int sz,time_t t){
    struct tm *tm=localtime(&t);strftime(o,sz,"%Y-%m-%d %H:%M",tm);
}
static void FmtDur(char *o,int sz,time_t sec){
    if(sec<60)snprintf(o,sz,"%I64ds",(long long)sec);
    else if(sec<3600)snprintf(o,sz,"%I64dm",(long long)(sec/60));
    else snprintf(o,sz,"%I64dh %I64dm",(long long)(sec/3600),(long long)((sec%3600)/60));
}

/* ============================================================================
 * Wide-char helpers for Unicode windows
 * ============================================================================ */
static const WCHAR *MakeW(const char *ascii){
    static WCHAR buf[256];int i;
    for(i=0;ascii[i]&&i<255;i++)buf[i]=(WCHAR)ascii[i];
    buf[i]=0;return buf;
}

/* ============================================================================
 * System tray (Unicode)
 * ============================================================================ */
static void AddTrayIcon(HWND hwnd){
    NOTIFYICONDATAW nid;memset(&nid,0,sizeof(nid));
    nid.cbSize=sizeof(NOTIFYICONDATAW);nid.hWnd=hwnd;nid.uID=TRAY_UID;
    nid.uFlags=NIF_ICON|NIF_MESSAGE|NIF_TIP;
    nid.uCallbackMessage=WM_TRAYICON;nid.hIcon=LoadIconW(NULL,MAKEINTRESOURCEW(32512));
    lstrcpyW(nid.szTip,L10NW(K_TRAY_TIP));
    Shell_NotifyIconW(NIM_ADD,&nid);
}
static void RemoveTrayIcon(HWND hwnd){
    NOTIFYICONDATAW nid;memset(&nid,0,sizeof(nid));
    nid.cbSize=sizeof(NOTIFYICONDATAW);nid.hWnd=hwnd;nid.uID=TRAY_UID;
    Shell_NotifyIconW(NIM_DELETE,&nid);
}
static void ShowTrayMenu(HWND hwnd){
    HMENU h=CreatePopupMenu();
    AppendMenuW(h,MF_STRING,IDM_SHOW,L10NW(K_MENU_SHOW));
    AppendMenuW(h,MF_STRING,IDM_CLEANUP,L10NW(K_MENU_CLEANUP));
    AppendMenuW(h,MF_SEPARATOR,0,NULL);
    AppendMenuW(h,MF_STRING,IDM_EXIT,L10NW(K_MENU_EXIT));
    POINT pt;GetCursorPos(&pt);SetForegroundWindow(hwnd);
    TrackPopupMenu(h,TPM_RIGHTBUTTON,pt.x,pt.y,0,hwnd,NULL);
    DestroyMenu(h);
}
static void ShowTrayBalloon(HWND hwnd,const WCHAR *title,const WCHAR *msg){
    NOTIFYICONDATAW nid;memset(&nid,0,sizeof(nid));
    nid.cbSize=sizeof(NOTIFYICONDATAW);nid.hWnd=hwnd;nid.uID=TRAY_UID;
    nid.uFlags=NIF_INFO;
    lstrcpyW(nid.szInfoTitle,title);
    lstrcpyW(nid.szInfo,msg);
    nid.dwInfoFlags=NIIF_INFO;
    Shell_NotifyIconW(NIM_MODIFY,&nid);
}

/* ============================================================================
 * GDI Bar Chart
 * ============================================================================ */
static void DrawGdiBarChart(HDC hdc,RECT rc,int range){
    int w=rc.right-rc.left,h=rc.bottom-rc.top;
    if(w<=0||h<=0)return;
    HBRUSH hBg=CreateSolidBrush(CLR_CARD);
    FillRect(hdc,&rc,hBg);DeleteObject(hBg);

    int pt=36,pb=44,pl=60,pr=36;
    int pw=w-pl-pr,ph=h-pt-pb;
    if(pw<=0||ph<=0)return;

    AggBucket *buckets=NULL;int count=0,maxTake=12;
    EnterCriticalSection(&g_csData);
    switch(range){
    case CHART_DAY:  buckets=g_hourlyBuckets;count=g_hourlyCount;maxTake=12;break;
    case CHART_WEEK: buckets=g_dailyBuckets;count=g_dailyCount;maxTake=7;break;
    case CHART_MONTH:buckets=g_dailyBuckets;count=g_dailyCount;maxTake=12;break;
    case CHART_YEAR: buckets=g_monthlyBuckets;count=g_monthlyCount;maxTake=12;break;
    }
    int take=count<maxTake?count:maxTake;
    int start=count-take;if(start<0){start=0;take=count;}
    AggBucket *plot=(AggBucket*)HeapAlloc(GetProcessHeap(),0,take*sizeof(AggBucket));
    if(plot)memcpy(plot,buckets+start,take*sizeof(AggBucket));
    LeaveCriticalSection(&g_csData);
    if(!plot||take<1){if(plot)HeapFree(GetProcessHeap(),0,plot);return;}

    /* Threshold line */
    HPEN hThr=CreatePen(PS_DASH,1,CLR_ORANGE);
    HPEN hOld=(HPEN)SelectObject(hdc,hThr);
    int thrY=pt+ph-(ph*85/100);
    MoveToEx(hdc,pl,thrY,NULL);LineTo(hdc,pl+pw,thrY);
    SelectObject(hdc,hOld);DeleteObject(hThr);
    SetTextColor(hdc,CLR_ORANGE);SetBkMode(hdc,TRANSPARENT);
    RECT thrR={pl+pw-40,thrY-16,pl+pw-2,thrY-2};
    DrawTextA(hdc,"85%",-1,&thrR,DT_RIGHT|DT_BOTTOM|DT_SINGLELINE);

    /* Grid */
    HPEN hGd=CreatePen(PS_SOLID,1,CLR_BORDER);
    hOld=(HPEN)SelectObject(hdc,hGd);
    int g;
    for(g=0;g<=100;g+=25){
        int y=pt+ph-(ph*g/100);
        MoveToEx(hdc,pl,y,NULL);LineTo(hdc,pl+pw,y);
        char lb[8];snprintf(lb,sizeof(lb),"%d%%",g);
        SetTextColor(hdc,CLR_MUTED);
        RECT tr={2,y-8,pl-4,y+8};
        DrawTextA(hdc,lb,-1,&tr,DT_RIGHT|DT_VCENTER|DT_SINGLELINE);
    }
    MoveToEx(hdc,pl,pt+ph,NULL);LineTo(hdc,pl+pw,pt+ph);
    SelectObject(hdc,hOld);DeleteObject(hGd);

    /* Draw bars — groups of two (PF + PH) per bucket */
    int barGap=8,groupGap=12;
    int nGroups=take;
    int barsPerGroup=2;
    int totalBars=nGroups*barsPerGroup;
    int avail=pw-groupGap*(nGroups-1);
    int barW=avail/totalBars;
    if(barW<4)barW=4;

    int i;
    for(i=0;i<take;i++){
        double pfAvg=plot[i].sampleCount>0?plot[i].pfSum/plot[i].sampleCount:0;
        double phAvg=plot[i].sampleCount>0?plot[i].phSum/plot[i].sampleCount:0;
        int gx=pl+(int)((double)i/(take-1)*pw);
        int bx=gx-(totalBars*barW+(nGroups-1)*groupGap)/2+(nGroups-1)*groupGap/2;

        /* PF bar */
        int pfH=(int)(pfAvg/100.0*ph);
        int pfy=pt+ph-pfH;
        if(pfH<2)pfH=2;
        HBRUSH bPf=CreateSolidBrush(CLR_PF);
        RECT rPf={bx,pfy,bx+barW,pt+ph};
        FillRect(hdc,&rPf,bPf);DeleteObject(bPf);

        /* PH bar */
        int phH=(int)(phAvg/100.0*ph);
        int phy=pt+ph-phH;
        if(phH<2)phH=2;
        HBRUSH bPh=CreateSolidBrush(CLR_PH);
        RECT rPh={bx+barW+2,phy,bx+barW*2+2,pt+ph};
        FillRect(hdc,&rPh,bPh);DeleteObject(bPh);

        /* Value labels */
        char vl[8];
        snprintf(vl,sizeof(vl),"%.0f",pfAvg);
        SetTextColor(hdc,CLR_PF);
        RECT vlR={bx,pfy-16,bx+barW,pfy-2};
        DrawTextA(hdc,vl,-1,&vlR,DT_CENTER|DT_BOTTOM|DT_SINGLELINE);
    }

    /* X-axis labels */
    SetTextColor(hdc,CLR_MUTED);
    for(i=0;i<take;i++){
        if(take>8&&i%(take/4)!=0&&i!=take-1)continue;
        int x=pl+(int)((double)i/(take-1)*pw);
        char lb[32];struct tm *tm=localtime(&plot[i].bucketStart);
        if(range==CHART_DAY)strftime(lb,sizeof(lb),"%H:%M",tm);
        else if(range==CHART_YEAR)strftime(lb,sizeof(lb),"%b",tm);
        else strftime(lb,sizeof(lb),"%m/%d",tm);
        RECT tr={x-25,pt+ph+6,x+25,pt+ph+22};
        DrawTextA(hdc,lb,-1,&tr,DT_CENTER|DT_TOP|DT_SINGLELINE);
    }

    /* Legend */
    {
        RECT lg={pl,8,pl+160,26};
        HBRUSH hLg=CreateSolidBrush(CLR_CARD2);
        HPEN hLgP=CreatePen(PS_SOLID,1,CLR_BORDERLT);
        SelectObject(hdc,hLg);SelectObject(hdc,hLgP);
        RoundRect(hdc,lg.left,lg.top,lg.right,lg.bottom,8,8);
        DeleteObject(hLg);DeleteObject(hLgP);

        HBRUSH b1=CreateSolidBrush(CLR_PF);
        FillRect(hdc,&(RECT){pl+8,13,pl+20,21},b1);DeleteObject(b1);
        SetTextColor(hdc,CLR_TEXT);
        RECT l1={pl+24,11,pl+70,23};
        DrawTextA(hdc,L10N(K_CHART_LEGEND_PF),-1,&l1,DT_LEFT|DT_VCENTER|DT_SINGLELINE);

        HBRUSH b2=CreateSolidBrush(CLR_PH);
        FillRect(hdc,&(RECT){pl+78,13,pl+90,21},b2);DeleteObject(b2);
        RECT l2={pl+94,11,pl+140,23};
        DrawTextA(hdc,L10N(K_CHART_LEGEND_PH),-1,&l2,DT_LEFT|DT_VCENTER|DT_SINGLELINE);

        SelectObject(hdc,hOld);
    }
    HeapFree(GetProcessHeap(),0,plot);
}

/* ============================================================================
 * GDI Line Chart (simplified — no per-pixel gradient for perf)
 * ============================================================================ */
static void DrawGdiLineChart(HDC hdc,RECT rc,int range){
    int w=rc.right-rc.left,h=rc.bottom-rc.top;
    if(w<=0||h<=0)return;
    HBRUSH hBg=CreateSolidBrush(CLR_CARD);
    FillRect(hdc,&rc,hBg);DeleteObject(hBg);

    int pt=36,pb=44,pl=60,pr=36;
    int pw=w-pl-pr,ph=h-pt-pb;
    if(pw<=0||ph<=0)return;

    AggBucket *buckets=NULL;int count=0,maxTake=30;
    EnterCriticalSection(&g_csData);
    switch(range){
    case CHART_DAY:  buckets=g_hourlyBuckets;count=g_hourlyCount;maxTake=24;break;
    case CHART_WEEK: buckets=g_dailyBuckets;count=g_dailyCount;maxTake=7;break;
    case CHART_MONTH:buckets=g_dailyBuckets;count=g_dailyCount;maxTake=30;break;
    case CHART_YEAR: buckets=g_monthlyBuckets;count=g_monthlyCount;maxTake=12;break;
    }
    int take=count<maxTake?count:maxTake;
    int start=count-take;if(start<0){start=0;take=count;}
    AggBucket *plot=(AggBucket*)HeapAlloc(GetProcessHeap(),0,take*sizeof(AggBucket));
    if(plot)memcpy(plot,buckets+start,take*sizeof(AggBucket));
    LeaveCriticalSection(&g_csData);
    if(!plot||take<2){if(plot)HeapFree(GetProcessHeap(),0,plot);return;}

    /* 85% threshold */
    HPEN hThr=CreatePen(PS_DASH,1,CLR_ORANGE);
    HPEN hOld=(HPEN)SelectObject(hdc,hThr);
    int thrY=pt+ph-(ph*85/100);
    MoveToEx(hdc,pl,thrY,NULL);LineTo(hdc,pl+pw,thrY);
    SelectObject(hdc,hOld);DeleteObject(hThr);
    SetTextColor(hdc,CLR_ORANGE);SetBkMode(hdc,TRANSPARENT);
    RECT thrR={pl+pw-40,thrY-16,pl+pw-2,thrY-2};
    DrawTextA(hdc,"85%",-1,&thrR,DT_RIGHT|DT_BOTTOM|DT_SINGLELINE);

    /* Grid */
    HPEN hGd=CreatePen(PS_SOLID,1,CLR_BORDER);
    hOld=(HPEN)SelectObject(hdc,hGd);
    int g;
    for(g=0;g<=100;g+=25){
        int y=pt+ph-(ph*g/100);
        MoveToEx(hdc,pl,y,NULL);LineTo(hdc,pl+pw,y);
        char lb[8];snprintf(lb,sizeof(lb),"%d%%",g);
        SetTextColor(hdc,CLR_MUTED);
        RECT tr={2,y-8,pl-4,y+8};
        DrawTextA(hdc,lb,-1,&tr,DT_RIGHT|DT_VCENTER|DT_SINGLELINE);
    }
    MoveToEx(hdc,pl,pt+ph,NULL);LineTo(hdc,pl+pw,pt+ph);
    SelectObject(hdc,hOld);DeleteObject(hGd);

    /* Semi-transparent fill under PF (horizontal line stripes) */
    {
        int i;
        for(i=0;i<take-1;i++){
            int x0=pl+(int)((double)i/(take-1)*pw);
            int x1=pl+(int)((double)(i+1)/(take-1)*pw);
            double pf0=plot[i].sampleCount>0?plot[i].pfSum/plot[i].sampleCount:0;
            double pf1=plot[i+1].sampleCount>0?plot[i+1].pfSum/plot[i+1].sampleCount:0;
            int y0=pt+ph-(int)(pf0/100.0*ph);
            int y1=pt+ph-(int)(pf1/100.0*ph);
            /* thin fill stripe */
            HPEN hF=CreatePen(PS_SOLID,1,RGB(GetRValue(CLR_PF)/4,GetGValue(CLR_PF)/4,GetBValue(CLR_PF)/4));
            HPEN hOld2=(HPEN)SelectObject(hdc,hF);
            int x;
            for(x=x0;x<=x1;x+=2){
                double t=(double)(x-x0)/(x1-x0);
                int yy=(int)(y0+t*(y1-y0));
                MoveToEx(hdc,x,yy,NULL);LineTo(hdc,x,pt+ph);
            }
            SelectObject(hdc,hOld2);DeleteObject(hF);
        }
    }

    /* PF line */
    HPEN hPf=CreatePen(PS_SOLID,3,CLR_PF);
    hOld=(HPEN)SelectObject(hdc,hPf);
    int i;
    for(i=0;i<take;i++){
        double v=plot[i].sampleCount>0?plot[i].pfSum/plot[i].sampleCount:0;
        int x=pl+(int)((double)i/(take-1)*pw);
        int y=pt+ph-(int)(v/100.0*ph);
        if(y<pt)y=pt;if(y>pt+ph)y=pt+ph;
        if(i==0)MoveToEx(hdc,x,y,NULL);else LineTo(hdc,x,y);
    }
    SelectObject(hdc,hOld);DeleteObject(hPf);

    /* PH line */
    HPEN hPh=CreatePen(PS_DASH,2,CLR_PH);
    hOld=(HPEN)SelectObject(hdc,hPh);
    for(i=0;i<take;i++){
        double v=plot[i].sampleCount>0?plot[i].phSum/plot[i].sampleCount:0;
        int x=pl+(int)((double)i/(take-1)*pw);
        int y=pt+ph-(int)(v/100.0*ph);
        if(y<pt)y=pt;if(y>pt+ph)y=pt+ph;
        if(i==0)MoveToEx(hdc,x,y,NULL);else LineTo(hdc,x,y);
    }
    SelectObject(hdc,hOld);DeleteObject(hPh);

    /* Dot markers */
    int dotStep=take>12?take/6:1;
    for(i=0;i<take;i+=dotStep){
        double v=plot[i].sampleCount>0?plot[i].pfMax:0;
        int x=pl+(int)((double)i/(take-1)*pw);
        int y=pt+ph-(int)(v/100.0*ph);
        if(y<pt)y=pt;if(y>pt+ph)y=pt+ph;
        HBRUSH hD=CreateSolidBrush(CLR_PF);
        HPEN hDP=CreatePen(PS_SOLID,2,CLR_CARD);
        SelectObject(hdc,hD);SelectObject(hdc,hDP);
        Ellipse(hdc,x-4,y-4,x+4,y+4);
        SelectObject(hdc,hOld);DeleteObject(hD);DeleteObject(hDP);
        char mx[8];snprintf(mx,sizeof(mx),"%.0f%%",v);
        SetTextColor(hdc,CLR_PF);
        RECT mr={x-18,y-22,x+18,y-8};
        DrawTextA(hdc,mx,-1,&mr,DT_CENTER|DT_BOTTOM|DT_SINGLELINE);
    }

    /* X-axis labels */
    SetTextColor(hdc,CLR_MUTED);
    for(i=0;i<take;i++){
        if(take>12&&i%(take/6)!=0&&i!=take-1)continue;
        int x=pl+(int)((double)i/(take-1)*pw);
        char lb[32];struct tm *tm=localtime(&plot[i].bucketStart);
        if(range==CHART_DAY)strftime(lb,sizeof(lb),"%H:%M",tm);
        else if(range==CHART_YEAR)strftime(lb,sizeof(lb),"%b",tm);
        else strftime(lb,sizeof(lb),"%m/%d",tm);
        RECT tr={x-25,pt+ph+6,x+25,pt+ph+22};
        DrawTextA(hdc,lb,-1,&tr,DT_CENTER|DT_TOP|DT_SINGLELINE);
    }

    /* Legend */
    {
        RECT lg={pl,8,pl+180,26};
        HBRUSH hLg=CreateSolidBrush(CLR_CARD2);
        HPEN hLgP=CreatePen(PS_SOLID,1,CLR_BORDERLT);
        SelectObject(hdc,hLg);SelectObject(hdc,hLgP);
        RoundRect(hdc,lg.left,lg.top,lg.right,lg.bottom,8,8);
        DeleteObject(hLg);DeleteObject(hLgP);

        HBRUSH b1=CreateSolidBrush(CLR_PF);
        FillRect(hdc,&(RECT){pl+8,13,pl+20,21},b1);DeleteObject(b1);
        SetTextColor(hdc,CLR_TEXT);
        RECT l1={pl+24,11,pl+70,23};
        DrawTextA(hdc,L10N(K_CHART_LEGEND_PF),-1,&l1,DT_LEFT|DT_VCENTER|DT_SINGLELINE);

        HBRUSH b2=CreateSolidBrush(CLR_PH);
        FillRect(hdc,&(RECT){pl+78,13,pl+90,21},b2);DeleteObject(b2);
        RECT l2={pl+94,11,pl+140,23};
        DrawTextA(hdc,L10N(K_CHART_LEGEND_PH),-1,&l2,DT_LEFT|DT_VCENTER|DT_SINGLELINE);

        HBRUSH b3=CreateSolidBrush(CLR_ORANGE);
        FillRect(hdc,&(RECT){pl+148,13,pl+160,21},b3);DeleteObject(b3);
        RECT l3={pl+164,11,pl+210,23};
        DrawTextA(hdc,L10N(K_CHART_LEGEND_THR),-1,&l3,DT_LEFT|DT_VCENTER|DT_SINGLELINE);

        SelectObject(hdc,hOld);
    }
    HeapFree(GetProcessHeap(),0,plot);
}

/* ============================================================================
 * Overview panel — 4 metric cards
 * ============================================================================ */
static void DrawOverviewPanel(HDC hdc,RECT rc){
    int w=rc.right-rc.left,h2=rc.bottom-rc.top;
    if(w<=0||h2<=0)return;

    /* Background */
    HBRUSH hBg=CreateSolidBrush(CLR_CARD);
    HPEN hBdr=CreatePen(PS_SOLID,1,CLR_BORDER);
    SelectObject(hdc,hBg);SelectObject(hdc,hBdr);
    Rectangle(hdc,rc.left,rc.top,rc.right,rc.bottom);
    DeleteObject(hBg);DeleteObject(hBdr);

    /* Title */
    SetTextColor(hdc,CLR_TEXT);SetBkMode(hdc,TRANSPARENT);
    if(g_hTitleFnt)SelectObject(hdc,g_hTitleFnt);
    RECT tr={rc.left+12,rc.top+8,rc.right-12,rc.top+30};
    DrawTextA(hdc,L10N(K_CARD_TITLE),-1,&tr,DT_LEFT|DT_VCENTER|DT_SINGLELINE);

    int cardW=(w-48)/4;
    int cardH=h2-44;
    int c,cx=rc.left+12,cy=rc.top+36;

    DWORD pf=g_latestSnapshot.pageFilePct;
    DWORD ph=g_latestSnapshot.physLoad;
    DWORD idle=g_latestSnapshot.idleSeconds;
    time_t ut=time(NULL)-g_tStartTime;

    COLORREF pfClr=pf>85?CLR_RED:pf>60?CLR_YELLOW:CLR_GREEN;
    COLORREF phClr=ph>90?CLR_RED:ph>70?CLR_YELLOW:CLR_GREEN;

    for(c=0;c<4;c++){
        RECT card={cx,cy,cx+cardW,cy+cardH};
        HBRUSH hCb=CreateSolidBrush(CLR_CARD2);
        HPEN hCp=CreatePen(PS_SOLID,1,CLR_BORDERLT);
        SelectObject(hdc,hCb);SelectObject(hdc,hCp);
        RoundRect(hdc,card.left,card.top,card.right,card.bottom,6,6);
        DeleteObject(hCb);DeleteObject(hCp);

        const char *label;char value[32],sub[64];
        COLORREF valClr=CLR_TEXT;

        switch(c){
        case 0:
            label=L10N(K_CARD_PF_LABEL);
            snprintf(value,sizeof(value),"%lu%%",pf);valClr=pfClr;
            snprintf(sub,sizeof(sub),L10N(K_CARD_PF_SUB),PAGE_FILE_THRESHOLD_PCT);
            break;
        case 1:
            label=L10N(K_CARD_PH_LABEL);
            snprintf(value,sizeof(value),"%lu%%",ph);valClr=phClr;
            {
                char t[16],a[16];
                FmtMB(t,sizeof(t),g_latestSnapshot.totalPhys);
                FmtMB(a,sizeof(a),g_latestSnapshot.availPhys);
                snprintf(sub,sizeof(sub),L10N(K_CARD_PH_SUB),a,t);
            }
            break;
        case 2:
            label=L10N(K_CARD_IDLE_LABEL);
            FmtDur(value,sizeof(value),idle);valClr=CLR_ACCENT;
            snprintf(sub,sizeof(sub),L10N(K_CARD_IDLE_SUB),IDLE_THRESHOLD_SEC/60);
            break;
        case 3:
            label=L10N(K_CARD_UPTIME_LABEL);
            FmtDur(value,sizeof(value),ut);valClr=CLR_GREEN;
            snprintf(sub,sizeof(sub),L10N(K_CARD_UPTIME_SUB),g_httpPort,DB_FILE_NAME);
            break;
        }

        SetTextColor(hdc,CLR_MUTED);
        if(g_hFnt)SelectObject(hdc,g_hFnt);
        RECT lb={card.left+10,card.top+6,card.right-10,card.top+22};
        DrawTextA(hdc,label,-1,&lb,DT_LEFT|DT_VCENTER|DT_SINGLELINE);

        SetTextColor(hdc,valClr);
        if(g_hTitleFnt)SelectObject(hdc,g_hTitleFnt);
        RECT vr={card.left+10,card.top+26,card.right-10,card.top+52};
        DrawTextA(hdc,value,-1,&vr,DT_LEFT|DT_VCENTER|DT_SINGLELINE);

        SetTextColor(hdc,CLR_MUTED);
        if(g_hFnt)SelectObject(hdc,g_hFnt);
        RECT sr={card.left+10,card.top+cardH-22,card.right-10,card.top+cardH-6};
        DrawTextA(hdc,sub,-1,&sr,DT_LEFT|DT_VCENTER|DT_SINGLELINE);

        cx+=cardW+8;
    }
}

/* ============================================================================
 * Pane window procs
 * ============================================================================ */
static LRESULT CALLBACK OverviewProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
    switch(msg){
    case WM_PAINT:{PAINTSTRUCT ps;HDC hdc=BeginPaint(hwnd,&ps);RECT rc;GetClientRect(hwnd,&rc);
        DrawOverviewPanel(hdc,(RECT){4,4,rc.right-4,88});EndPaint(hwnd,&ps);return 0;}
    case WM_ERASEBKGND:return 1;
    }
    return DefWindowProcW(hwnd,msg,wp,lp);
}
static LRESULT CALLBACK ChartProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
    switch(msg){
    case WM_PAINT:{PAINTSTRUCT ps;HDC hdc=BeginPaint(hwnd,&ps);RECT rc;GetClientRect(hwnd,&rc);
        if(g_chartMode==0)DrawGdiLineChart(hdc,rc,g_range);
        else DrawGdiBarChart(hdc,rc,g_range);
        EndPaint(hwnd,&ps);return 0;}
    case WM_ERASEBKGND:return 1;
    }
    return DefWindowProcW(hwnd,msg,wp,lp);
}

/* ============================================================================
 * ListView update functions (all localized)
 * ============================================================================ */
static void UpdProc(void){
    if(!g_hProc)return;LvDelA(g_hProc);
    EnterCriticalSection(&g_csData);
    MemorySnapshot *s=&g_latestSnapshot;
    int i;
    for(i=0;i<s->numProcesses;i++){
        ProcessInfo *p=&s->topProcesses[i];
        char rk[8],pid[12],cm[32],ws[32],gr[32];
        snprintf(rk,sizeof(rk),"%d",i+1);
        snprintf(pid,sizeof(pid),"%lu",p->pid);
        FmtMB(cm,sizeof(cm),p->commitSize);FmtMB(ws,sizeof(ws),p->workingSet);
        snprintf(gr,sizeof(gr),p->growthRateMBps>0.01?"+%.1f MB/s":"-",p->growthRateMBps);
        LVITEMA it;memset(&it,0,sizeof(it));it.mask=LVIF_TEXT;it.iItem=i;it.pszText=rk;
        LvIns(g_hProc,&it);
        LvSetTxt(g_hProc,i,1,pid);LvSetTxt(g_hProc,i,2,p->name);
        LvSetTxt(g_hProc,i,3,cm);LvSetTxt(g_hProc,i,4,ws);LvSetTxt(g_hProc,i,5,gr);
    }
    LeaveCriticalSection(&g_csData);
}
static void UpdAnom(void){
    if(!g_hAnom)return;LvDelA(g_hAnom);
    EnterCriticalSection(&g_csData);
    int i;
    for(i=0;i<g_anomalyCount;i++){
        AnomalyAlert *a=&g_anomalies[i];
        char ts[32],ty[32],pid[12],vl[32];
        FmtTime(ts,sizeof(ts),a->timestamp);
        switch(a->type){
        case ANOMALY_CPU_HOG:strcpy(ty,L10N(K_ANOM_CPU_HOG));break;
        case ANOMALY_MEM_HOG:strcpy(ty,L10N(K_ANOM_MEM_HOG));break;
        case ANOMALY_MEM_LEAK:strcpy(ty,L10N(K_ANOM_MEM_LEAK));break;
        case ANOMALY_GPU_HOG:strcpy(ty,L10N(K_ANOM_GPU_HOG));break;
        case ANOMALY_SUSPICIOUS:strcpy(ty,L10N(K_ANOM_SUSPICIOUS));break;
        default:strcpy(ty,L10N(K_ANOM_UNKNOWN));break;
        }
        snprintf(pid,sizeof(pid),"%lu",a->pid);
        snprintf(vl,sizeof(vl),"%.1f",a->value);
        LVITEMA it;memset(&it,0,sizeof(it));it.mask=LVIF_TEXT;it.iItem=i;it.pszText=ts;
        LvIns(g_hAnom,&it);
        LvSetTxt(g_hAnom,i,1,ty);LvSetTxt(g_hAnom,i,2,pid);
        LvSetTxt(g_hAnom,i,3,a->procName);LvSetTxt(g_hAnom,i,4,vl);
        LvSetTxt(g_hAnom,i,5,a->description);
    }
    LeaveCriticalSection(&g_csData);
}
static void UpdSusp(void){
    if(!g_hSusp)return;LvDelA(g_hSusp);
    EnterCriticalSection(&g_csData);
    int i;
    for(i=0;i<g_suspProcCount;i++){
        SuspiciousProc *sp=&g_suspProcs[i];
        char pid[12],fst[32],lst[32],grw[32],rt[32],fs[32],ls[32],al[8];
        snprintf(pid,sizeof(pid),"%lu",sp->pid);
        FmtMB(fst,sizeof(fst),sp->firstCommit);FmtMB(lst,sizeof(lst),sp->lastCommit);
        SIZE_T gb=sp->lastCommit>sp->firstCommit?(sp->lastCommit-sp->firstCommit):0;
        FmtMB(grw,sizeof(grw),gb);
        snprintf(rt,sizeof(rt),"%.1f MB/s",sp->peakGrowthRate);
        FmtTime(fs,sizeof(fs),sp->firstSeen);FmtTime(ls,sizeof(ls),sp->lastSeen);
        snprintf(al,sizeof(al),"%d",sp->alertCount);
        LVITEMA it;memset(&it,0,sizeof(it));it.mask=LVIF_TEXT;it.iItem=i;it.pszText=pid;
        LvIns(g_hSusp,&it);
        LvSetTxt(g_hSusp,i,1,sp->name);LvSetTxt(g_hSusp,i,2,fst);LvSetTxt(g_hSusp,i,3,lst);
        LvSetTxt(g_hSusp,i,4,grw);LvSetTxt(g_hSusp,i,5,rt);
        LvSetTxt(g_hSusp,i,6,fs);LvSetTxt(g_hSusp,i,7,ls);LvSetTxt(g_hSusp,i,8,al);
    }
    LeaveCriticalSection(&g_csData);
}
static void UpdAct(void){
    if(!g_hAct)return;LvDelA(g_hAct);
    EnterCriticalSection(&g_csData);
    int i;
    for(i=0;i<g_actionCount;i++){
        ActionRecord *a=&g_actions[i];
        char ts[32],bf[16],af[16],tc[8],fc[8],desc[512];
        FmtTime(ts,sizeof(ts),a->timestamp);
        int delta=(int)a->pageFileBefore-(int)a->pageFileAfter;
        snprintf(bf,sizeof(bf),"%lu%%",a->pageFileBefore);
        snprintf(af,sizeof(af),"%lu%%",a->pageFileAfter);
        snprintf(tc,sizeof(tc),"%d",a->trimmedCount);
        snprintf(fc,sizeof(fc),"%d",a->failedCount);
        snprintf(desc,sizeof(desc),"%s [%s%d%%]",
            a->description,delta>0?"-":"+",abs(delta));
        LVITEMA it;memset(&it,0,sizeof(it));it.mask=LVIF_TEXT;it.iItem=i;it.pszText=ts;
        LvIns(g_hAct,&it);
        LvSetTxt(g_hAct,i,1,bf);LvSetTxt(g_hAct,i,2,af);
        LvSetTxt(g_hAct,i,3,tc);LvSetTxt(g_hAct,i,4,fc);LvSetTxt(g_hAct,i,5,desc);
    }
    LeaveCriticalSection(&g_csData);
}
static void Refresh(HWND hwnd){(void)hwnd;
    UpdProc();UpdAnom();UpdSusp();UpdAct();
    if(g_hOvPane)InvalidateRect(g_hOvPane,NULL,TRUE);
    if(g_hChart)InvalidateRect(g_hChart,NULL,TRUE);
}

/* ============================================================================
 * Tab management
 * ============================================================================ */
typedef enum{TAB_OV=0,TAB_PR,TAB_CH,TAB_AN,TAB_SU}TabIdx;
static void ShowTab(TabIdx t){
    int ov=(t==TAB_OV),pr=(t==TAB_PR),ch=(t==TAB_CH),an=(t==TAB_AN),su=(t==TAB_SU);
    ShowWindow(g_hOvPane,ov?SW_SHOW:SW_HIDE);
    ShowWindow(g_hProc,(ov||pr)?SW_SHOW:SW_HIDE);
    ShowWindow(g_hAct,ov?SW_SHOW:SW_HIDE);
    ShowWindow(g_hChart,ch?SW_SHOW:SW_HIDE);
    ShowWindow(g_hCbo,ch?SW_SHOW:SW_HIDE);
    ShowWindow(g_hLblCbo,ch?SW_SHOW:SW_HIDE);
    ShowWindow(g_hBtnBar,ch?SW_SHOW:SW_HIDE);
    ShowWindow(g_hAnom,an?SW_SHOW:SW_HIDE);
    ShowWindow(g_hSusp,su?SW_SHOW:SW_HIDE);
    if(ch)InvalidateRect(g_hChart,NULL,TRUE);
    if(ov)InvalidateRect(g_hOvPane,NULL,TRUE);
}

/* ============================================================================
 * ListView creation (localized headers)
 * ============================================================================ */
static HWND MakeLV(HWND p,int x,int y,int w,int h,const char **hdr,int n,int *wd){
    HWND lv=CreateWindowExW(0,WC_LISTVIEWW,NULL,
        WS_CHILD|WS_VISIBLE|LVS_REPORT|LVS_SINGLESEL|LVS_SHOWSELALWAYS,
        x,y,w,h,p,NULL,GetModuleHandleA(NULL),NULL);
    LvExSt(lv,LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER|LVS_EX_GRIDLINES);
    LV_COLUMNA col;int i;
    for(i=0;i<n;i++){
        memset(&col,0,sizeof(col));
        col.mask=LVCF_TEXT|LVCF_WIDTH|LVCF_FMT;col.fmt=LVCFMT_LEFT;
        col.pszText=(LPSTR)hdr[i];col.cx=wd[i];
        LvInsCol(lv,i,&col);
    }
    ListView_SetBkColor(lv,CLR_CARD);
    ListView_SetTextBkColor(lv,CLR_CARD);
    ListView_SetTextColor(lv,CLR_TEXT2);
    return lv;
}

/* ============================================================================
 * Status bar
 * ============================================================================ */
static void UpdSts(HWND hwnd){(void)hwnd;
    if(!g_hSts)return;char txt[512];
    DWORD pf=g_latestSnapshot.pageFilePct,ph=g_latestSnapshot.physLoad;
    DWORD idle=g_latestSnapshot.idleSeconds;
    snprintf(txt,sizeof(txt),L10N(K_STATUS_FMT),pf,ph,idle,g_httpPort);
    int bl=(int)strlen(txt);
    FmtDur(txt+bl,sizeof(txt)-bl,time(NULL)-g_tStartTime);
    SendMessageA(g_hSts,SB_SETTEXTA,0,(LPARAM)txt);
}

/* ============================================================================
 * Window class names (constants, not localized)
 * ============================================================================ */
static const WCHAR *WC_MAIN=L"VMManagerDesktopW";
static const WCHAR *WC_OV=L"VMOverviewPaneW";
static const WCHAR *WC_CHART=L"VMChartAreaW";

/* ============================================================================
 * WndProc (Unicode)
 * ============================================================================ */
static LRESULT CALLBACK WndProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
    switch(msg){
    case WM_CREATE:{
        InitCommonControls();
        g_hDsk=hwnd;
        EnableDarkTitleBar(hwnd);

        /* CJK-capable fonts with ANTIALIASED_QUALITY for GDI */
        {
            LocaleId loc=LocaleGet();
            const char *face=(loc==LOC_ZH_TW)?"Microsoft JhengHei":"Microsoft YaHei";
            g_hFnt=CreateFontA(14,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,
                DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,
                CLEARTYPE_NATURAL_QUALITY,FF_DONTCARE,face);
            g_hTitleFnt=CreateFontA(18,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,
                DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,
                CLEARTYPE_NATURAL_QUALITY,FF_DONTCARE,face);
            g_hMonoFnt=CreateFontA(13,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,
                DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,
                CLEARTYPE_NATURAL_QUALITY,FF_DONTCARE,"Consolas");
        }

        RECT rc;GetClientRect(hwnd,&rc);
        int cw=rc.right-rc.left,ch=rc.bottom-rc.top;

        /* Tab */
        g_hTab=CreateWindowExW(0,WC_TABCONTROLW,NULL,
            WS_CHILD|WS_VISIBLE|TCS_FIXEDWIDTH,
            8,6,cw-16,28,hwnd,(HMENU)IDC_TAB,
            GetModuleHandleA(NULL),NULL);
        SendMessageW(g_hTab,WM_SETFONT,(WPARAM)g_hFnt,TRUE);
        {
            TCITEMA tci;memset(&tci,0,sizeof(tci));tci.mask=TCIF_TEXT;
            tci.pszText=(LPSTR)L10N(K_TAB_OVERVIEW);
            TcIns(g_hTab,TAB_OV,&tci);
            tci.pszText=(LPSTR)L10N(K_TAB_PROCESSES);
            TcIns(g_hTab,TAB_PR,&tci);
            tci.pszText=(LPSTR)L10N(K_TAB_CHARTS);
            TcIns(g_hTab,TAB_CH,&tci);
            tci.pszText=(LPSTR)L10N(K_TAB_ANOMALIES);
            TcIns(g_hTab,TAB_AN,&tci);
            tci.pszText=(LPSTR)L10N(K_TAB_SUSPICIOUS);
            TcIns(g_hTab,TAB_SU,&tci);
        }

        /* Buttons */
        g_hClnup=CreateWindowExW(0,L"BUTTON",L10NW(K_BTN_CLEANUP),
            WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON|BS_FLAT,
            cw-340,8,100,26,hwnd,(HMENU)IDC_BTN_CLEANUP,
            GetModuleHandleA(NULL),NULL);
        SendMessageW(g_hClnup,WM_SETFONT,(WPARAM)g_hFnt,TRUE);

        HWND btnX=CreateWindowExW(0,L"BUTTON",L10NW(K_BTN_EXIT),
            WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON|BS_FLAT,
            cw-120,8,100,26,hwnd,(HMENU)IDC_BTN_EXIT,
            GetModuleHandleA(NULL),NULL);
        SendMessageW(btnX,WM_SETFONT,(WPARAM)g_hFnt,TRUE);

        /* Chart controls (hidden by default) */
        g_hLblCbo=CreateWindowExW(0,L"STATIC",L10NW(K_CHART_RANGE_LBL),
            WS_CHILD|SS_LEFT,16,42,80,20,hwnd,NULL,
            GetModuleHandleA(NULL),NULL);
        SendMessageW(g_hLblCbo,WM_SETFONT,(WPARAM)g_hFnt,TRUE);
        ShowWindow(g_hLblCbo,SW_HIDE);

        g_hCbo=CreateWindowExW(0,L"COMBOBOX",NULL,
            WS_CHILD|CBS_DROPDOWNLIST|WS_VSCROLL,
            100,40,135,200,hwnd,(HMENU)IDC_COMBO_RANGE,
            GetModuleHandleA(NULL),NULL);
        SendMessageW(g_hCbo,WM_SETFONT,(WPARAM)g_hFnt,TRUE);
        SendMessageW(g_hCbo,CB_ADDSTRING,0,(LPARAM)L10NW(K_CHART_RANGE_DAY));
        SendMessageW(g_hCbo,CB_ADDSTRING,0,(LPARAM)L10NW(K_CHART_RANGE_WEEK));
        SendMessageW(g_hCbo,CB_ADDSTRING,0,(LPARAM)L10NW(K_CHART_RANGE_MONTH));
        SendMessageW(g_hCbo,CB_ADDSTRING,0,(LPARAM)L10NW(K_CHART_RANGE_YEAR));
        SendMessageW(g_hCbo,CB_SETCURSEL,CHART_WEEK,0);
        ShowWindow(g_hCbo,SW_HIDE);

        /* Bar/Line toggle button */
        g_hBtnBar=CreateWindowExW(0,L"BUTTON",L"Line / Bar",
            WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON|BS_FLAT,
            250,40,80,22,hwnd,(HMENU)5001,
            GetModuleHandleA(NULL),NULL);
        SendMessageW(g_hBtnBar,WM_SETFONT,(WPARAM)g_hFnt,TRUE);
        ShowWindow(g_hBtnBar,SW_HIDE);

        int ct=44,cl=8,cright=cw-16;

        /* Register custom pane classes (Unicode) */
        {
            WNDCLASSW cls;memset(&cls,0,sizeof(cls));
            cls.lpfnWndProc=OverviewProc;cls.hInstance=GetModuleHandleA(NULL);
            cls.hCursor=LoadCursorA(NULL,(LPCSTR)IDC_ARROW);
            cls.hbrBackground=CreateSolidBrush(CLR_BG);
            cls.lpszClassName=WC_OV;RegisterClassW(&cls);
        }
        {
            WNDCLASSW cls;memset(&cls,0,sizeof(cls));
            cls.lpfnWndProc=ChartProc;cls.hInstance=GetModuleHandleA(NULL);
            cls.hCursor=LoadCursorA(NULL,(LPCSTR)IDC_ARROW);
            cls.hbrBackground=CreateSolidBrush(CLR_CARD);
            cls.lpszClassName=WC_CHART;RegisterClassW(&cls);
        }

        /* Overview pane */
        g_hOvPane=CreateWindowExW(0,WC_OV,NULL,
            WS_CHILD|WS_VISIBLE,cl,ct,cright,88,
            hwnd,NULL,GetModuleHandleA(NULL),NULL);

        /* Process list */
        {
            const char *hdr[]={L10N(K_HDR_RANK),L10N(K_HDR_PID),L10N(K_HDR_NAME),
                L10N(K_HDR_COMMIT),L10N(K_HDR_WS),L10N(K_HDR_GROWTH)};
            int wd[]={36,70,180,110,110,100};
            g_hProc=MakeLV(hwnd,cl,ct+96,cright,ch-ct-350,hdr,6,wd);
        }

        /* Action log */
        {
            const char *hdr[]={L10N(K_HDR_TIME),L10N(K_HDR_BEFORE),L10N(K_HDR_AFTER),
                L10N(K_HDR_TRIMMED),L10N(K_HDR_FAILED),L10N(K_HDR_DESC)};
            int wd[]={130,60,70,60,50,260};
            g_hAct=MakeLV(hwnd,cl,ch-242,cright,200,hdr,6,wd);
        }

        /* Chart area */
        g_hChart=CreateWindowExW(0,WC_CHART,NULL,
            WS_CHILD|WS_VISIBLE|WS_BORDER,
            cl,68,cright,ch-100,hwnd,(HMENU)IDC_CHART_AREA,
            GetModuleHandleA(NULL),NULL);

        /* Anomaly list */
        {
            const char *hdr[]={L10N(K_HDR_TIME),L10N(K_HDR_TYPE),L10N(K_HDR_PID),
                L10N(K_HDR_PROCESS),L10N(K_HDR_VALUE),L10N(K_HDR_DESC)};
            int wd[]={130,90,55,130,65,300};
            g_hAnom=MakeLV(hwnd,cl,ct,cright,ch-ct-28,hdr,6,wd);
        }

        /* Suspicious list */
        {
            const char *hdr[]={L10N(K_HDR_PID),L10N(K_HDR_NAME),L10N(K_HDR_FIRST),
                L10N(K_HDR_LAST),L10N(K_HDR_GROWTH),L10N(K_HDR_PEAK_RATE),
                L10N(K_HDR_FIRST_SEEN),L10N(K_HDR_LAST_SEEN),L10N(K_HDR_ALERTS)};
            int wd[]={55,120,95,95,90,85,125,125,50};
            g_hSusp=MakeLV(hwnd,cl,ct,cright,ch-ct-28,hdr,9,wd);
        }

        /* Status bar */
        g_hSts=CreateWindowExW(0,STATUSCLASSNAMEW,NULL,
            WS_CHILD|WS_VISIBLE|SBARS_SIZEGRIP,
            0,0,0,0,hwnd,NULL,GetModuleHandleA(NULL),NULL);
        SendMessageW(g_hSts,WM_SETFONT,(WPARAM)g_hFnt,TRUE);

        ShowTab(TAB_OV);
        AddTrayIcon(hwnd);
        SetTimer(hwnd,IDT_DESKTOP_REFRESH,DESKTOP_REFRESH_MS,NULL);
        return 0;
    }

    case WM_NOTIFY:{
        NMHDR *nm=(NMHDR*)lp;
        if(nm->idFrom==IDC_TAB&&nm->code==TCN_SELCHANGE){
            int sel=TcSel(g_hTab);
            switch(sel){
            case 0:ShowTab(TAB_OV);break;case 1:ShowTab(TAB_PR);break;
            case 2:ShowTab(TAB_CH);break;case 3:ShowTab(TAB_AN);break;
            case 4:ShowTab(TAB_SU);break;
            }
            Refresh(hwnd);
        }
        if(nm->code==NM_CUSTOMDRAW){
            NMLVCUSTOMDRAW *lcd=(NMLVCUSTOMDRAW*)lp;
            if(nm->idFrom==GetDlgCtrlID(g_hAnom)){
                switch(lcd->nmcd.dwDrawStage){
                case CDDS_PREPAINT:
                    SetWindowLongPtrA(hwnd,DWLP_MSGRESULT,CDRF_NOTIFYITEMDRAW);
                    return CDRF_NOTIFYITEMDRAW;
                case CDDS_ITEMPREPAINT:{
                    int idx=(int)lcd->nmcd.dwItemSpec;
                    lcd->clrTextBk=idx%2==0?CLR_CARD:CLR_CARD2;lcd->clrText=CLR_TEXT2;
                    AnomalyAlert *a2=NULL;
                    EnterCriticalSection(&g_csData);
                    if(idx<g_anomalyCount)a2=&g_anomalies[idx];
                    LeaveCriticalSection(&g_csData);
                    if(a2){
                        COLORREF sev=CLR_RED;
                        if(a2->type==ANOMALY_MEM_HOG||a2->type==ANOMALY_GPU_HOG)sev=CLR_YELLOW;
                        RECT rc2;ListView_GetItemRect(nm->hwndFrom,idx,&rc2,LVIR_BOUNDS);
                        HDC hdc=lcd->nmcd.hdc;
                        HBRUSH hb=CreateSolidBrush(sev);
                        RECT bar={rc2.left,rc2.top,rc2.left+3,rc2.bottom};
                        FillRect(hdc,&bar,hb);DeleteObject(hb);
                    }
                    SetWindowLongPtrA(hwnd,DWLP_MSGRESULT,CDRF_NEWFONT);
                    return CDRF_NEWFONT;
                }
                }
            }
            if(nm->idFrom==GetDlgCtrlID(g_hProc)||
               nm->idFrom==GetDlgCtrlID(g_hSusp)||
               nm->idFrom==GetDlgCtrlID(g_hAct)){
                switch(lcd->nmcd.dwDrawStage){
                case CDDS_PREPAINT:
                    SetWindowLongPtrA(hwnd,DWLP_MSGRESULT,CDRF_NOTIFYITEMDRAW);
                    return CDRF_NOTIFYITEMDRAW;
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

    case WM_COMMAND:{
        WORD id=LOWORD(wp);
        switch(id){
        case IDC_BTN_CLEANUP:CheckAndAct();Refresh(hwnd);break;
        case IDC_BTN_EXIT:DestroyWindow(hwnd);break;
        case 5001:/* bar/line toggle */
            g_chartMode=!g_chartMode;
            SetWindowTextW(g_hBtnBar,g_chartMode?L"Bar" : L"Line");
            InvalidateRect(g_hChart,NULL,TRUE);
            break;
        case IDC_COMBO_RANGE:
            if(HIWORD(wp)==CBN_SELCHANGE){
                g_range=(int)SendMessageW((HWND)lp,CB_GETCURSEL,0,0);
                InvalidateRect(g_hChart,NULL,TRUE);
            }
            break;
        case IDM_SHOW:ShowWindow(hwnd,SW_RESTORE);SetForegroundWindow(hwnd);break;
        case IDM_CLEANUP:CheckAndAct();Refresh(hwnd);break;
        case IDM_EXIT:DestroyWindow(hwnd);break;
        }
        return 0;
    }

    case WM_TRAYICON:
        if(LOWORD(lp)==WM_RBUTTONUP)ShowTrayMenu(hwnd);
        else if(LOWORD(lp)==WM_LBUTTONDBLCLK){
            ShowWindow(hwnd,SW_RESTORE);SetForegroundWindow(hwnd);
        }
        return 0;

    case WM_SYSCOMMAND:
        if((wp&0xFFF0)==SC_MINIMIZE){
            ShowWindow(hwnd,SW_HIDE);
            ShowTrayBalloon(hwnd,L"VM Manager",L10NW(K_TRAY_BALLOON_MIN));
            return 0;
        }
        break;

    case WM_CLOSE:
        ShowWindow(hwnd,SW_HIDE);
        ShowTrayBalloon(hwnd,L"VM Manager",L10NW(K_TRAY_BALLOON_CLOSE));
        return 0;

    case WM_DESTROY:
        KillTimer(hwnd,IDT_DESKTOP_REFRESH);RemoveTrayIcon(hwnd);PostQuitMessage(0);
        return 0;

    case WM_TIMER:
        if(wp==IDT_DESKTOP_REFRESH){Refresh(hwnd);UpdSts(hwnd);}
        return 0;

    case WM_CTLCOLORSTATIC:
        SetTextColor((HDC)wp,CLR_MUTED);SetBkColor((HDC)wp,CLR_BG);
        return (LRESULT)CreateSolidBrush(CLR_BG);
    case WM_CTLCOLORBTN:
        SetTextColor((HDC)wp,CLR_TEXT2);SetBkColor((HDC)wp,CLR_CARD2);
        return (LRESULT)CreateSolidBrush(CLR_CARD2);
    case WM_CTLCOLORLISTBOX:
        SetTextColor((HDC)wp,CLR_TEXT2);SetBkColor((HDC)wp,CLR_CARD);
        return (LRESULT)CreateSolidBrush(CLR_CARD);

    case WM_SIZE:{
        RECT rc;GetClientRect(hwnd,&rc);
        int cw=rc.right-rc.left,ch=rc.bottom-rc.top;
        int ct=44,cl=8,cright=cw-16;
        if(g_hTab)      SetWindowPos(g_hTab,NULL,8,6,cw-16,28,SWP_NOZORDER);
        if(g_hClnup)    SetWindowPos(g_hClnup,NULL,cw-340,8,100,26,SWP_NOZORDER);
        {HWND btn=GetDlgItem(hwnd,IDC_BTN_EXIT);if(btn)SetWindowPos(btn,NULL,cw-120,8,100,26,SWP_NOZORDER);}
        if(g_hOvPane)   SetWindowPos(g_hOvPane,NULL,cl,ct,cright,88,SWP_NOZORDER);
        if(g_hProc)     SetWindowPos(g_hProc,NULL,cl,ct+96,cright,ch-ct-350,SWP_NOZORDER);
        if(g_hAct)      SetWindowPos(g_hAct,NULL,cl,ch-242,cright,200,SWP_NOZORDER);
        if(g_hChart)    SetWindowPos(g_hChart,NULL,cl,68,cright,ch-100,SWP_NOZORDER);
        if(g_hAnom)     SetWindowPos(g_hAnom,NULL,cl,ct,cright,ch-ct-28,SWP_NOZORDER);
        if(g_hSusp)     SetWindowPos(g_hSusp,NULL,cl,ct,cright,ch-ct-28,SWP_NOZORDER);
        SendMessageW(g_hSts,WM_SIZE,0,0);
        return 0;
    }
    }
    return DefWindowProcW(hwnd,msg,wp,lp);
}

/* ============================================================================
 * RunDesktop (Unicode window creation)
 * ============================================================================ */
int RunDesktop(void){
    g_bDesktop=TRUE;

    WNDCLASSW wc;memset(&wc,0,sizeof(wc));
    wc.lpfnWndProc=WndProc;
    wc.hInstance=GetModuleHandleA(NULL);
    wc.hCursor=LoadCursorA(NULL,(LPCSTR)IDC_ARROW);
    wc.hbrBackground=CreateSolidBrush(CLR_BG);
    wc.lpszClassName=WC_MAIN;
    wc.hIcon=LoadIconA(NULL,(LPCSTR)IDI_APPLICATION);
    if(!RegisterClassW(&wc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return 1;

    HWND hwnd=CreateWindowExW(0,WC_MAIN,
        L10NW(K_WIN_TITLE_MAIN),
        WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,
        CW_USEDEFAULT,CW_USEDEFAULT,1080,760,
        NULL,NULL,GetModuleHandleA(NULL),NULL);
    if(!hwnd)return 1;

    ShowWindow(hwnd,SW_SHOW);UpdateWindow(hwnd);

    if(!g_hHttpThread){
        g_hHttpThread=CreateThread(NULL,0,HttpServerThread,NULL,0,NULL);
        int wc2=0;while(g_httpPort==0&&wc2<30){Sleep(100);wc2++;}
    }

    CheckAndAct();

    MSG msg;
    while(GetMessageW(&msg,NULL,0,0)){TranslateMessage(&msg);DispatchMessageW(&msg);}

    g_bRunning=FALSE;
    RemoveTrayIcon(hwnd);
    return 0;
}
