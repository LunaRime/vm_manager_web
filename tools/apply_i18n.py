#!/usr/bin/env python
"""Apply L10N() wrappers to vm_desktop.c hardcoded strings."""
import sys, os

base = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
fp = os.path.join(base, 'src', 'vm_desktop.c')

with open(fp, 'r', encoding='utf-8', errors='replace') as f:
    src = f.read()

reps = [
    ('"  Overview  "',  'L10N(K_TAB_OVERVIEW)'),
    ('"  Processes  "', 'L10N(K_TAB_PROCESSES)'),
    ('"  Charts  "',    'L10N(K_TAB_CHARTS)'),
    ('"  Anomalies  "', 'L10N(K_TAB_ANOMALIES)'),
    ('"  Suspicious  "','L10N(K_TAB_SUSPICIOUS)'),
    ('"Cleanup Now"',   'L10N(K_BTN_CLEANUP)'),
    ('"Exit Program"',  'L10N(K_BTN_EXIT)'),
    ('"Time Range:"',   'L10N(K_CHART_RANGE_LBL)'),
    ('"Past 24 Hours"', 'L10N(K_CHART_RANGE_DAY)'),
    ('"Past Week"',     'L10N(K_CHART_RANGE_WEEK)'),
    ('"Past Month"',    'L10N(K_CHART_RANGE_MONTH)'),
    ('"Past Year"',     'L10N(K_CHART_RANGE_YEAR)'),
    ('DrawTextA(hdc,"Page File",-1,',   'DrawTextA(hdc,L10N(K_CHART_LEGEND_PF),-1,'),
    ('DrawTextA(hdc,"Physical",-1,',    'DrawTextA(hdc,L10N(K_CHART_LEGEND_PH),-1,'),
    ('DrawTextA(hdc,"Threshold",-1,',   'DrawTextA(hdc,L10N(K_CHART_LEGEND_THR),-1,'),
    ('DrawTextA(hdc,"System Status Dashboard",-1,', 'DrawTextA(hdc,L10N(K_CARD_TITLE),-1,'),
    ('label="Page File Usage"',  'label=L10N(K_CARD_PF_LABEL)'),
    ('label="Physical Memory"',  'label=L10N(K_CARD_PH_LABEL)'),
    ('label="System Idle"',      'label=L10N(K_CARD_IDLE_LABEL)'),
    ('label="Uptime"',           'label=L10N(K_CARD_UPTIME_LABEL)'),
    ('"Threshold: %d%%"',        'L10N(K_CARD_PF_SUB)'),
    ('"Auto-clean after %dm idle"', 'L10N(K_CARD_IDLE_SUB)'),
    ('"%s free / %s total"',     'L10N(K_CARD_PH_SUB)'),
    ('"Dashboard: :%d | DB: %s"','L10N(K_CARD_UPTIME_SUB)'),
    ('lstrcpyA((LPSTR)nid.szTip,"VM Manager \\u2014 Memory Monitor")',
     'lstrcpyA((LPSTR)nid.szTip,L10N(K_TRAY_TIP))'),
    ('"Minimized to system tray.\\nDouble-click to restore."',
     'L10N(K_TRAY_BALLOON_MIN)'),
    ('"Still running in background.\\nRight-click tray icon to exit."',
     'L10N(K_TRAY_BALLOON_CLOSE)'),
    ('"&Show Window"',   'L10N(K_MENU_SHOW)'),
    ('"&Manual Cleanup"', 'L10N(K_MENU_CLEANUP)'),
    ('"E&xit"',          'L10N(K_MENU_EXIT)'),
    ('"VM Manager v4.1 \\u2014 Memory Monitor \\u0026 Suspicious Process Detector"',
     'L10N(K_WIN_TITLE_MAIN)'),
    ('"  Page File: %lu%%  |  Physical: %lu%%  |  Idle: %lus  |  Port: %d  |  Uptime: "',
     'L10N(K_STATUS_FMT)'),
]

for old, new in reps:
    src = src.replace(old, new)

with open(fp, 'w', encoding='utf-8', errors='replace') as f:
    f.write(src)

print(f'Updated {fp} with {len(reps)} i18n replacements')
