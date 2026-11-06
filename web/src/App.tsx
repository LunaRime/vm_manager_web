import {
  createContext, useCallback, useContext, useEffect, useRef, useState,
} from 'react';
import {
  Activity, Bell, Database, LayoutDashboard, ListTree, Moon, RefreshCw,
  Settings as SettingsIcon, Sun, Wifi, WifiOff,
} from 'lucide-react';
import { api, ApiError, type SaveSettingsResponse } from './api';
import { formatTime, LANGUAGES, t } from './i18n';
import AlertsPage from './pages/AlertsPage';
import HistoryPage from './pages/HistoryPage';
import OverviewPage from './pages/OverviewPage';
import ProcessesPage from './pages/ProcessesPage';
import SettingsPage from './pages/SettingsPage';
import type {
  ActionLog, AggregatedPoint, AlertEvent, AlertRule, HealthResponse,
  HistoryPoint, Language, Settings, SettingsMetaItem, StatsResponse,
  StatusResponse, Theme, WhitelistEntry,
} from './types';

export type Page = 'overview' | 'processes' | 'history' | 'alerts' | 'settings';

interface ToastItem { id: number; title: string; body: string; kind: 'info' | 'warning' | 'critical' }
interface ConfirmState { title: string; message: string; resolve: (value: boolean) => void }

export interface AppState {
  lang: Language;
  theme: Theme;
  t: (key: string) => string;
  page: Page;
  setPage: (page: Page) => void;
  status: StatusResponse | null;
  settings: Settings | null;
  defaults: Settings | null;
  settingsMeta: SettingsMetaItem[];
  platform: string;
  alerts: AlertEvent[];
  rules: AlertRule[];
  whitelist: WhitelistEntry[];
  actions: ActionLog[];
  history: HistoryPoint[];
  historyRange: string;
  setHistoryRange: (range: string) => void;
  aggregated: AggregatedPoint[];
  aggRange: string;
  setAggRange: (range: string) => void;
  stats: StatsResponse | null;
  health: HealthResponse | null;
  connected: boolean;
  loading: boolean;
  error: string | null;
  refreshAll: () => Promise<void>;
  refreshStatus: () => Promise<void>;
  refreshAlerts: () => Promise<void>;
  refreshRules: () => Promise<void>;
  refreshWhitelist: () => Promise<void>;
  refreshActions: () => Promise<void>;
  refreshHistory: () => Promise<void>;
  refreshAggregated: () => Promise<void>;
  saveSettings: (patch: Partial<Settings>) => Promise<SaveSettingsResponse>;
  toast: (title: string, body: string, kind?: ToastItem['kind']) => void;
  confirm: (title: string, message: string) => Promise<boolean>;
  playSound: () => void;
}

const AppContext = createContext<AppState | null>(null);

export function useApp(): AppState {
  const ctx = useContext(AppContext);
  if (!ctx) throw new Error('useApp must be used inside App');
  return ctx;
}

const NAV: Array<{ id: Page; icon: typeof Activity; key: string }> = [
  { id: 'overview', icon: LayoutDashboard, key: 'nav_overview' },
  { id: 'processes', icon: ListTree, key: 'nav_processes' },
  { id: 'history', icon: Database, key: 'nav_history' },
  { id: 'alerts', icon: Bell, key: 'nav_alerts' },
  { id: 'settings', icon: SettingsIcon, key: 'nav_settings' },
];

const rank = (s: string): number => (s === 'critical' ? 2 : s === 'warning' ? 1 : 0);
const errMsg = (e: unknown): string =>
  e instanceof ApiError ? e.message : e instanceof Error ? e.message : String(e);

export default function App() {
  const initialHash = window.location.hash.slice(1) as Page;
  const [page, setPageState] = useState<Page>(NAV.some((n) => n.id === initialHash) ? initialHash : 'overview');
  const [lang, setLang] = useState<Language>('zh-CN');
  const [theme, setTheme] = useState<Theme>('light');
  const [status, setStatus] = useState<StatusResponse | null>(null);
  const [settings, setSettings] = useState<Settings | null>(null);
  const [defaults, setDefaults] = useState<Settings | null>(null);
  const [settingsMeta, setSettingsMeta] = useState<SettingsMetaItem[]>([]);
  const [platform, setPlatform] = useState('');
  const [alerts, setAlerts] = useState<AlertEvent[]>([]);
  const [rules, setRules] = useState<AlertRule[]>([]);
  const [whitelist, setWhitelist] = useState<WhitelistEntry[]>([]);
  const [actions, setActions] = useState<ActionLog[]>([]);
  const [history, setHistory] = useState<HistoryPoint[]>([]);
  const [historyRange, setHistoryRange] = useState('24h');
  const [aggregated, setAggregated] = useState<AggregatedPoint[]>([]);
  const [aggRange, setAggRange] = useState('week');
  const [stats, setStats] = useState<StatsResponse | null>(null);
  const [health, setHealth] = useState<HealthResponse | null>(null);
  const [connected, setConnected] = useState(false);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);
  const [toasts, setToasts] = useState<ToastItem[]>([]);
  const [confirmState, setConfirmState] = useState<ConfirmState | null>(null);

  const langRef = useRef(lang); langRef.current = lang;
  const settingsRef = useRef(settings); settingsRef.current = settings;
  const connectedRef = useRef(connected); connectedRef.current = connected;
  const historyRangeRef = useRef(historyRange); historyRangeRef.current = historyRange;
  const aggRangeRef = useRef(aggRange); aggRangeRef.current = aggRange;
  const initializedRef = useRef(false);

  useEffect(() => { document.documentElement.setAttribute('data-theme', theme); }, [theme]);

  const setPage = useCallback((next: Page) => {
    setPageState(next);
    window.location.hash = next;
  }, []);

  const toast = useCallback((title: string, body: string, kind: ToastItem['kind'] = 'info') => {
    const id = Date.now() + Math.random();
    setToasts((prev) => [...prev.slice(-3), { id, title, body, kind }]);
    setTimeout(() => setToasts((prev) => prev.filter((item) => item.id !== id)), 6500);
  }, []);

  const confirm = useCallback((title: string, message: string) =>
    new Promise<boolean>((resolve) => setConfirmState({ title, message, resolve })), []);

  const playSound = useCallback(() => {
    try {
      const Ctor = window.AudioContext || (window as unknown as { webkitAudioContext: typeof AudioContext }).webkitAudioContext;
      const ctx = new Ctor();
      const osc = ctx.createOscillator();
      const gain = ctx.createGain();
      osc.connect(gain); gain.connect(ctx.destination);
      osc.type = 'sine'; osc.frequency.value = 880;
      gain.gain.setValueAtTime(0.0001, ctx.currentTime);
      gain.gain.exponentialRampToValueAtTime(0.11, ctx.currentTime + 0.02);
      gain.gain.exponentialRampToValueAtTime(0.0001, ctx.currentTime + 0.2);
      osc.start(); osc.stop(ctx.currentTime + 0.22);
      setTimeout(() => void ctx.close(), 500);
    } catch { /* audio is optional */ }
  }, []);

  const refreshStatus = useCallback(async () => {
    try { setStatus(await api.status()); setError(null); } catch (e) { setError(errMsg(e)); }
  }, []);
  const refreshHealth = useCallback(async () => {
    try { setHealth(await api.health()); } catch { setConnected(false); }
  }, []);
  const refreshAlerts = useCallback(async () => {
    try { setAlerts((await api.alerts()).alerts); } catch { /* keep previous */ }
  }, []);
  const refreshRules = useCallback(async () => {
    try { setRules((await api.rules()).rules); } catch { /* keep previous */ }
  }, []);
  const refreshWhitelist = useCallback(async () => {
    try { setWhitelist((await api.whitelist()).entries); } catch { /* keep previous */ }
  }, []);
  const refreshActions = useCallback(async () => {
    try { setActions((await api.actions()).actions); } catch { /* keep previous */ }
  }, []);
  const refreshHistory = useCallback(async () => {
    try { setHistory((await api.history(historyRangeRef.current)).points); } catch { /* keep previous */ }
  }, []);
  const refreshAggregated = useCallback(async () => {
    try { setAggregated((await api.aggregated(aggRangeRef.current)).points); } catch { /* keep previous */ }
  }, []);
  const refreshStats = useCallback(async () => {
    try { setStats(await api.stats()); } catch { /* keep previous */ }
  }, []);
  const refreshSettings = useCallback(async () => {
    try {
      const res = await api.settings();
      setSettings(res.settings); setDefaults(res.defaults);
      setSettingsMeta(res.meta); setPlatform(res.platform);
      if (!initializedRef.current) {
        initializedRef.current = true;
        setLang(res.settings.language);
        setTheme(res.settings.theme);
      }
    } catch { /* keep previous */ }
  }, []);

  const refreshAll = useCallback(async () => {
    setLoading(true);
    await Promise.all([
      refreshHealth(), refreshStatus(), refreshSettings(), refreshAlerts(),
      refreshRules(), refreshWhitelist(), refreshActions(),
      refreshHistory(), refreshAggregated(), refreshStats(),
    ]);
    setLoading(false);
  }, [refreshHealth, refreshStatus, refreshSettings, refreshAlerts, refreshRules,
    refreshWhitelist, refreshActions, refreshHistory, refreshAggregated, refreshStats]);

  const saveSettings = useCallback(async (patch: Partial<Settings>): Promise<SaveSettingsResponse> => {
    if (patch.language) setLang(patch.language);
    if (patch.theme) setTheme(patch.theme);
    const res = await api.saveSettings(patch);
    setSettings(res.settings);
    return res;
  }, []);

  useEffect(() => { void refreshAll(); }, [refreshAll]);

  useEffect(() => { void refreshHistory(); }, [historyRange, refreshHistory]);
  useEffect(() => { void refreshAggregated(); }, [aggRange, refreshAggregated]);

  // SSE drives all live updates; polling is only a fallback when the stream drops.
  useEffect(() => {
    const es = new EventSource('/api/events');
    es.onopen = () => { setConnected(true); setError(null); };
    es.onerror = () => setConnected(false);
    const onSnapshot = () => {
      void refreshStatus(); void refreshHistory(); void refreshAggregated();
      void refreshStats(); void refreshHealth();
    };
    const onAlert = (ev: Event) => {
      void refreshAlerts();
      try {
        const alert = JSON.parse((ev as MessageEvent).data) as AlertEvent;
        const s = settingsRef.current;
        const min = s?.notifyMinSeverity ?? 'warning';
        if (s?.notifyWeb !== false && rank(alert.severity) >= rank(min)) {
          toast(t('toast_new_alert', langRef.current), alert.message, alert.severity);
          if (s?.notifySound) playSound();
        }
      } catch { /* malformed payload */ }
    };
    es.addEventListener('snapshot', onSnapshot);
    es.addEventListener('alert', onAlert);
    es.addEventListener('settings', () => { void refreshSettings(); });
    es.addEventListener('action', () => { void refreshActions(); void refreshWhitelist(); });
    return () => es.close();
  }, [refreshStatus, refreshHistory, refreshAggregated, refreshStats, refreshHealth,
    refreshAlerts, refreshSettings, refreshActions, refreshWhitelist, toast, playSound]);

  useEffect(() => {
    const id = window.setInterval(() => {
      if (!connectedRef.current) { void refreshStatus(); void refreshAlerts(); }
    }, 10_000);
    return () => window.clearInterval(id);
  }, [refreshStatus, refreshAlerts]);

  const unacked = alerts.filter((a) => !a.acknowledged).length;
  const tr = (key: string) => t(key, lang);
  const navTitle = tr(NAV.find((n) => n.id === page)?.key ?? 'nav_overview');

  const value: AppState = {
    lang, theme, t: tr, page, setPage, status, settings, defaults, settingsMeta, platform,
    alerts, rules, whitelist, actions, history, historyRange, setHistoryRange,
    aggregated, aggRange, setAggRange, stats, health, connected, loading, error,
    refreshAll, refreshStatus, refreshAlerts, refreshRules, refreshWhitelist,
    refreshActions, refreshHistory, refreshAggregated, saveSettings, toast, confirm, playSound,
  };

  return (
    <AppContext.Provider value={value}>
      <div className="app-shell">
        <aside className="sidebar">
          <div className="brand">
            <div className="brand-icon"><Activity size={20} /></div>
            <div><h1>{tr('app_title')}</h1><p>{tr('app_subtitle')}</p></div>
          </div>
          <nav className="nav">
            {NAV.map((item) => {
              const Icon = item.icon;
              return (
                <button key={item.id} className={`nav-item ${page === item.id ? 'active' : ''}`}
                  onClick={() => setPage(item.id)}>
                  <Icon size={17} />{tr(item.key)}
                  {item.id === 'alerts' && unacked > 0 && <span className="badge-count">{unacked}</span>}
                </button>
              );
            })}
          </nav>
          <div className="sidebar-footer">
            v6.0.0 · 127.0.0.1:{settings?.port ?? 18080}<br />
            {tr('last_update')}: {status ? formatTime(status.ts, lang).slice(-8) : '--:--:--'}
          </div>
        </aside>

        <main className="main">
          <header className="topbar">
            <h2>{navTitle}</h2>
            <span className="pill">
              {connected ? <Wifi size={13} color="var(--ok)" /> : <WifiOff size={13} color="var(--danger)" />}
              {connected ? tr('connected') : tr('disconnected')}
            </span>
            <div className="spacer" />
            <div className="segmented">
              {LANGUAGES.map((code) => (
                <button key={code} className={lang === code ? 'active' : ''}
                  onClick={() => void saveSettings({ language: code }).catch((e) => toast(tr('set_save_failed'), errMsg(e), 'critical'))}>
                  {code === 'en' ? 'EN' : code === 'zh-TW' ? '繁' : '简'}
                </button>
              ))}
            </div>
            <button className="btn icon" title={tr('toggle_theme')}
              onClick={() => void saveSettings({ theme: theme === 'light' ? 'dark' : 'light' }).catch(() => {})}>
              {theme === 'light' ? <Moon size={16} /> : <Sun size={16} />}
            </button>
            <button className="btn icon" title={tr('refresh')} onClick={() => void refreshAll()}>
              <RefreshCw size={16} />
            </button>
          </header>

          <div className="content">
            {status?.demo && (
              <div className="banner warn"><Bell size={15} /><b>{tr('demo_mode')}</b> — {tr('demo_hint')}</div>
            )}
            {status?.collectorError && (
              <div className="banner error"><WifiOff size={15} /><b>{tr('collector_error')}</b> — {status.collectorError}</div>
            )}
            {!connected && error && (
              <div className="banner error"><WifiOff size={15} />{tr('error_conn')}: {error}</div>
            )}
            {loading && <div className="empty">{tr('loading')}</div>}
            {!loading && (
              <div className="fade" key={page}>
                {page === 'overview' && <OverviewPage />}
                {page === 'processes' && <ProcessesPage />}
                {page === 'history' && <HistoryPage />}
                {page === 'alerts' && <AlertsPage />}
                {page === 'settings' && <SettingsPage />}
              </div>
            )}
          </div>
        </main>
      </div>

      <div className="toast-stack">
        {toasts.map((item) => (
          <div key={item.id} className={`toast ${item.kind}`}>
            <div className="t-title">{item.title}</div>
            <div className="t-body">{item.body}</div>
          </div>
        ))}
      </div>

      {confirmState && (
        <div className="overlay" onClick={() => { confirmState.resolve(false); setConfirmState(null); }}>
          <div className="modal" onClick={(e) => e.stopPropagation()}>
            <h3>{confirmState.title}</h3>
            <p>{confirmState.message}</p>
            <div className="modal-actions">
              <button className="btn" onClick={() => { confirmState.resolve(false); setConfirmState(null); }}>
                {tr('cancel')}
              </button>
              <button className="btn danger" onClick={() => { confirmState.resolve(true); setConfirmState(null); }}>
                {tr('confirm')}
              </button>
            </div>
          </div>
        </div>
      )}
    </AppContext.Provider>
  );
}
