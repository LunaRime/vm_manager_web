/**
 * vm_i18n.hpp — C++ internationalization layer
 *
 * Wraps the C locale engine (src/core/vm_locale.c) with:
 *   - std::string return values (no ring-buffer lifetime issues)
 *   - Type-safe key constants in a namespace
 *   - Formatted string support with variadic templates
 *   - Singleton access pattern
 *
 * Languages: en-US, zh-CN (Simplified), zh-TW (Traditional)
 */
#ifndef VM_I18N_HPP
#define VM_I18N_HPP

#include "vm_bridge.hpp"
#include <string>
#include <cstdio>
#include <cstdarg>

// ============================================================================
// I18n key namespace — every user-facing string gets a named constant
// ============================================================================
namespace i18n {

/* ---- Window & App ---- */
constexpr const char *APP_TITLE        = "app_title";
constexpr const char *APP_SUBTITLE     = "app_subtitle";
constexpr const char *APP_VERSION      = "app_version";
constexpr const char *APP_ARCH         = "app_arch";

/* ---- Tabs ---- */
constexpr const char *TAB_OVERVIEW     = "tab_overview";
constexpr const char *TAB_PROCESSES    = "tab_processes";
constexpr const char *TAB_DATABASE     = "tab_database";
constexpr const char *TAB_CHARTS       = "tab_charts";
constexpr const char *TAB_ANOMALIES    = "tab_anomalies";
constexpr const char *TAB_SUSPICIOUS   = "tab_suspicious";

/* ---- Buttons ---- */
constexpr const char *BTN_CLEANUP      = "btn_cleanup";
constexpr const char *BTN_EXIT         = "btn_exit";
constexpr const char *BTN_EXPORT       = "btn_export";
constexpr const char *BTN_REFRESH      = "btn_refresh";
constexpr const char *BTN_SETTINGS     = "btn_settings";
constexpr const char *BTN_CHART_LINE   = "btn_chart_line";
constexpr const char *BTN_CHART_BAR    = "btn_chart_bar";

/* ---- Chart ---- */
constexpr const char *CHART_RANGE_LBL  = "chart_range_lbl";
constexpr const char *CHART_RANGE_DAY  = "chart_range_day";
constexpr const char *CHART_RANGE_WEEK = "chart_range_week";
constexpr const char *CHART_RANGE_MONTH= "chart_range_month";
constexpr const char *CHART_RANGE_YEAR = "chart_range_year";
constexpr const char *CHART_LEGEND_PF  = "chart_legend_pf";
constexpr const char *CHART_LEGEND_PH  = "chart_legend_ph";
constexpr const char *CHART_LEGEND_THR = "chart_legend_thr";
constexpr const char *CHART_THRESHOLD  = "chart_threshold";

/* ---- Cards ---- */
constexpr const char *CARD_TITLE       = "card_title";
constexpr const char *CARD_PF_LABEL    = "card_pf_label";
constexpr const char *CARD_PH_LABEL    = "card_ph_label";
constexpr const char *CARD_IDLE_LABEL  = "card_idle_label";
constexpr const char *CARD_UPTIME_LABEL= "card_uptime_label";
constexpr const char *CARD_PF_SUB      = "card_pf_sub";
constexpr const char *CARD_PH_SUB      = "card_ph_sub";
constexpr const char *CARD_IDLE_SUB    = "card_idle_sub";
constexpr const char *CARD_UPTIME_SUB  = "card_uptime_sub";

/* ---- Database ---- */
constexpr const char *DB_TITLE         = "db_title";
constexpr const char *DB_RECORDS       = "db_records";
constexpr const char *DB_SIZE          = "db_size";
constexpr const char *DB_DATE_RANGE    = "db_date_range";
constexpr const char *DB_OLDEST        = "db_oldest";
constexpr const char *DB_NEWEST        = "db_newest";
constexpr const char *DB_EXPORT_CSV    = "db_export_csv";
constexpr const char *DB_EXPORT_DONE   = "db_export_done";
constexpr const char *DB_NO_DATA       = "db_no_data";
constexpr const char *DB_LOADING       = "db_loading";

/* ---- System tray ---- */
constexpr const char *TRAY_TIP         = "tray_tip";
constexpr const char *TRAY_BALLOON_MIN = "tray_balloon_min";
constexpr const char *TRAY_BALLOON_CLOSE= "tray_balloon_close";

/* ---- Menu ---- */
constexpr const char *MENU_SHOW        = "menu_show";
constexpr const char *MENU_CLEANUP     = "menu_cleanup";
constexpr const char *MENU_EXIT        = "menu_exit";
constexpr const char *MENU_EXPORT      = "menu_export";
constexpr const char *MENU_ABOUT       = "menu_about";

/* ---- Status bar ---- */
constexpr const char *STATUS_FMT       = "status_fmt";
constexpr const char *STATUS_READY     = "status_ready";
constexpr const char *STATUS_CLEANING  = "status_cleaning";
constexpr const char *STATUS_EXPORTING = "status_exporting";

/* ---- ListView headers ---- */
constexpr const char *HDR_RANK         = "hdr_rank";
constexpr const char *HDR_PID          = "hdr_pid";
constexpr const char *HDR_NAME         = "hdr_name";
constexpr const char *HDR_COMMIT       = "hdr_commit";
constexpr const char *HDR_WS           = "hdr_ws";
constexpr const char *HDR_GROWTH       = "hdr_growth";
constexpr const char *HDR_TIME         = "hdr_time";
constexpr const char *HDR_TYPE         = "hdr_type";
constexpr const char *HDR_VALUE        = "hdr_value";
constexpr const char *HDR_DESC         = "hdr_desc";
constexpr const char *HDR_PROCESS      = "hdr_process";
constexpr const char *HDR_BEFORE       = "hdr_before";
constexpr const char *HDR_AFTER        = "hdr_after";
constexpr const char *HDR_TRIMMED      = "hdr_trimmed";
constexpr const char *HDR_FAILED       = "hdr_failed";
constexpr const char *HDR_FIRST        = "hdr_first";
constexpr const char *HDR_LAST         = "hdr_last";
constexpr const char *HDR_PEAK_RATE    = "hdr_peak_rate";
constexpr const char *HDR_FIRST_SEEN   = "hdr_first_seen";
constexpr const char *HDR_LAST_SEEN    = "hdr_last_seen";
constexpr const char *HDR_ALERTS       = "hdr_alerts";

/* ---- Anomaly types ---- */
constexpr const char *ANOM_CPU_HOG     = "anom_cpu_hog";
constexpr const char *ANOM_MEM_HOG     = "anom_mem_hog";
constexpr const char *ANOM_MEM_LEAK    = "anom_mem_leak";
constexpr const char *ANOM_GPU_HOG     = "anom_gpu_hog";
constexpr const char *ANOM_SUSPICIOUS  = "anom_suspicious";
constexpr const char *ANOM_UNKNOWN     = "anom_unknown";

/* ---- Misc ---- */
constexpr const char *MSG_NO_DATA      = "msg_no_data";
constexpr const char *MSG_LOADING      = "msg_loading";
constexpr const char *MSG_ERROR        = "msg_error";
constexpr const char *MSG_EXPORTING    = "msg_exporting";
constexpr const char *MSG_CLEANUP_DONE = "msg_cleanup_done";
constexpr const char *LBL_SEARCH       = "lbl_search";
constexpr const char *LBL_FILTER       = "lbl_filter";
constexpr const char *LBL_ALL          = "lbl_all";

} // namespace i18n

// ============================================================================
// VMI18n — C++ i18n singleton
// ============================================================================
class VMI18n {
public:
    static VMI18n &Instance();

    /**
     * Look up a localized string by key.
     * Returns std::string (safe copy, no lifetime issues).
     */
    std::string Get(const char *key) const;

    /**
     * Formatted lookup: sprintf-style formatting.
     */
    std::string Format(const char *key, ...) const;

    /**
     * Look up as wide string (for Win32 Unicode APIs).
     * Returns a pointer to an internal buffer — copy immediately.
     */
    const WCHAR *GetW(const char *key) const;

    /**
     * Formatted wide string lookup.
     */
    const WCHAR *FormatW(const char *key, ...) const;

    /** Get current locale code. */
    LocaleId GetLocale() const;

    /** Set locale (e.g., for user override). */
    void SetLocale(LocaleId id);

    /** Get locale name string. */
    const char *GetLocaleName() const;

private:
    VMI18n();
    ~VMI18n() = default;
    VMI18n(const VMI18n &) = delete;
    VMI18n &operator=(const VMI18n &) = delete;

public:
    /* String table entry.
       Usage: { "key", { "en", "zhCN", "zhTW" } }
       The inner array stores values for LOC_EN, LOC_ZH_CN, LOC_ZH_TW. */
    struct Entry {
        const char *key;
        const char *values[3];
    };

private:

    static const Entry *FindEntry(const char *key);
    std::string Utf8ToAnsi(const char *utf8) const;
    std::wstring Utf8ToWide(const char *utf8) const;
};

#endif /* VM_I18N_HPP */
