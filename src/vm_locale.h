/**
 * vm_locale.h — Localization / internationalization for VM Manager v4.1
 *
 * Auto-detects system language at startup.
 * All user-facing strings go through L10N() / L10NF() macros.
 *
 * Supported languages:
 *   zh-CN   Simplified Chinese  (PRC, Singapore)
 *   zh-TW   Traditional Chinese (Taiwan, Hong Kong, Macau)
 *   en-US   English (default fallback)
 *
 * Detection priority:
 *   1. GetUserDefaultUILanguage()  → map LANGID → locale code
 *   2. GetSystemDefaultLangID()    → map LANGID → locale code
 *   3. en-US fallback
 */

#ifndef VM_LOCALE_H
#define VM_LOCALE_H

/* ============================================================================
 * Locale codes
 * ============================================================================ */
typedef enum {
    LOC_EN = 0,   /* English (default)              */
    LOC_ZH_CN,    /* Simplified Chinese              */
    LOC_ZH_TW,    /* Traditional Chinese             */
    LOC_COUNT
} LocaleId;

/* ============================================================================
 * Public API
 * ============================================================================ */

/** Detect system locale once at startup. Call before any L10N() usage. */
void LocaleInit(void);

/** Return the active locale id. */
LocaleId LocaleGet(void);

/** Set locale manually (e.g., for testing or user override). */
void LocaleSet(LocaleId id);

/**
 * Look up a localized string by key.
 * Returns "??key??" if key not found (safe fallback).
 */
const char *L10N(const char *key);

/**
 * Formatted lookup: sprintf the key, return localized string.
 * Convenience wrapper — not reentrant (uses shared static buffer).
 * Use for dynamic keys like "page_%d" → "Page %d" / "第%d页".
 */
const char *L10NF(const char *keyFmt, ...);

/* ============================================================================
 * String keys — every user-facing string in the application
 *
 * Naming convention:
 *   win_title      — window titles
 *   tab_*          — tab labels
 *   btn_*          — button labels
 *   hdr_*          — listview column headers
 *   lbl_*          — static labels
 *   tray_*         — system tray
 *   menu_*         — context menu items
 *   card_*         — overview cards
 *   chart_*        — chart UI
 *   anom_*         — anomaly type names
 *   log_*          — log messages (printf format strings)
 *   web_*          — web dashboard strings
 * ============================================================================ */
#define K_WIN_TITLE          "win_title"
#define K_WIN_TITLE_MAIN     "win_title_main"
#define K_TAB_OVERVIEW       "tab_overview"
#define K_TAB_PROCESSES      "tab_processes"
#define K_TAB_CHARTS         "tab_charts"
#define K_TAB_ANOMALIES      "tab_anomalies"
#define K_TAB_SUSPICIOUS     "tab_suspicious"
#define K_BTN_CLEANUP        "btn_cleanup"
#define K_BTN_EXIT           "btn_exit"
#define K_CHART_RANGE_LBL    "chart_range_lbl"
#define K_CHART_RANGE_DAY    "chart_range_day"
#define K_CHART_RANGE_WEEK   "chart_range_week"
#define K_CHART_RANGE_MONTH  "chart_range_month"
#define K_CHART_RANGE_YEAR   "chart_range_year"
#define K_CHART_LEGEND_PF    "chart_legend_pf"
#define K_CHART_LEGEND_PH    "chart_legend_ph"
#define K_CHART_LEGEND_THR   "chart_legend_thr"
#define K_CARD_TITLE         "card_title"
#define K_CARD_PF_LABEL      "card_pf_label"
#define K_CARD_PH_LABEL      "card_ph_label"
#define K_CARD_IDLE_LABEL    "card_idle_label"
#define K_CARD_UPTIME_LABEL  "card_uptime_label"
#define K_CARD_PF_SUB        "card_pf_sub"
#define K_CARD_PH_SUB        "card_ph_sub"
#define K_CARD_IDLE_SUB      "card_idle_sub"
#define K_CARD_UPTIME_SUB    "card_uptime_sub"
#define K_TRAY_TIP           "tray_tip"
#define K_TRAY_BALLOON_MIN   "tray_balloon_min"
#define K_TRAY_BALLOON_CLOSE "tray_balloon_close"
#define K_MENU_SHOW          "menu_show"
#define K_MENU_CLEANUP       "menu_cleanup"
#define K_MENU_EXIT          "menu_exit"
#define K_STATUS_FMT         "status_fmt"

/* ListView headers */
#define K_HDR_RANK           "hdr_rank"
#define K_HDR_PID            "hdr_pid"
#define K_HDR_NAME           "hdr_name"
#define K_HDR_COMMIT         "hdr_commit"
#define K_HDR_WS             "hdr_ws"
#define K_HDR_GROWTH         "hdr_growth"
#define K_HDR_TIME           "hdr_time"
#define K_HDR_TYPE           "hdr_type"
#define K_HDR_VALUE          "hdr_value"
#define K_HDR_DESC           "hdr_desc"
#define K_HDR_PROCESS        "hdr_process"
#define K_HDR_BEFORE         "hdr_before"
#define K_HDR_AFTER          "hdr_after"
#define K_HDR_TRIMMED        "hdr_trimmed"
#define K_HDR_FAILED         "hdr_failed"
#define K_HDR_FIRST          "hdr_first"
#define K_HDR_LAST           "hdr_last"
#define K_HDR_PEAK_RATE      "hdr_peak_rate"
#define K_HDR_FIRST_SEEN     "hdr_first_seen"
#define K_HDR_LAST_SEEN      "hdr_last_seen"
#define K_HDR_ALERTS         "hdr_alerts"

/* Anomaly type labels */
#define K_ANOM_CPU_HOG       "anom_cpu_hog"
#define K_ANOM_MEM_HOG       "anom_mem_hog"
#define K_ANOM_MEM_LEAK      "anom_mem_leak"
#define K_ANOM_GPU_HOG       "anom_gpu_hog"
#define K_ANOM_SUSPICIOUS    "anom_suspicious"
#define K_ANOM_UNKNOWN       "anom_unknown"

/* Engine log messages */
#define K_LOG_STARTING       "log_starting"
#define K_LOG_HEADLESS       "log_headless"
#define K_LOG_CONSOLE        "log_console"
#define K_LOG_DASHBOARD      "log_dashboard"
#define K_LOG_CHECK_INFO     "log_check_info"
#define K_LOG_HEARTBEAT      "log_heartbeat"
#define K_LOG_TRIGGERED      "log_triggered"
#define K_LOG_CLEANUP_START  "log_cleanup_start"
#define K_LOG_CLEANUP_RESULT "log_cleanup_result"
#define K_LOG_CLEANUP_DONE   "log_cleanup_done"
#define K_LOG_CLEANUP_END    "log_cleanup_end"
#define K_LOG_COOLDOWN       "log_cooldown"
#define K_LOG_CPU_ANOM       "log_cpu_anom"
#define K_LOG_MEM_ANOM       "log_mem_anom"
#define K_LOG_GPU_ANOM       "log_gpu_anom"
#define K_LOG_SUSP_ANOM      "log_susp_anom"
#define K_LOG_DB_LOADED      "log_db_loaded"
#define K_LOG_STOPPED        "log_stopped"
#define K_LOG_HTTP_FAIL      "log_http_fail"
#define K_LOG_PRIV_GRANTED   "log_priv_granted"
#define K_LOG_PRIV_DENIED    "log_priv_denied"
#define K_LOG_GPU_INIT       "log_gpu_init"
#define K_LOG_GPU_UNAVAIL    "log_gpu_unavail"
#define K_LOG_CPU_CORES      "log_cpu_cores"
#define K_LOG_DB_WARN        "log_db_warn"

/* Anomaly descriptions */
#define K_ANOM_CPU_DESC      "anom_cpu_desc"
#define K_ANOM_MEM_DESC      "anom_mem_desc"
#define K_ANOM_GPU_DESC      "anom_gpu_desc"
#define K_ANOM_SUSP_DESC     "anom_susp_desc"
#define K_ACTION_DESC        "action_desc"

#endif /* VM_LOCALE_H */
