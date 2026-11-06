import { useState } from 'react';
import { AlertTriangle, Database, RefreshCw, Save, Trash2 } from 'lucide-react';
import { useApp } from '../App';
import { api, ApiError } from '../api';
import { formatTime, LANGUAGES } from '../i18n';
import type { Settings, Severity } from '../types';
import { Card } from '../ui';

const errMsg = (e: unknown): string =>
  e instanceof ApiError ? e.message : e instanceof Error ? e.message : String(e);

type FieldType = 'bool' | 'number' | 'lang' | 'theme' | 'severity';
interface FieldSpec {
  key: keyof Settings;
  group: string;
  type: FieldType;
  min?: number;
  max?: number;
  hint?: string;
}

const GROUPS = ['collect', 'service', 'appearance', 'notify', 'data', 'alert'];

const SCHEMA: FieldSpec[] = [
  { key: 'sampleIntervalSec', group: 'collect', type: 'number', min: 5, max: 3600 },
  { key: 'growthThresholdMbps', group: 'alert', type: 'number', min: 1, max: 1024 },
  { key: 'port', group: 'service', type: 'number', min: 1024, max: 65535 },
  { key: 'autoStart', group: 'service', type: 'bool', hint: 'set_autostart_hint' },
  { key: 'language', group: 'appearance', type: 'lang' },
  { key: 'theme', group: 'appearance', type: 'theme' },
  { key: 'notifyWeb', group: 'notify', type: 'bool' },
  { key: 'notifySound', group: 'notify', type: 'bool' },
  { key: 'notifyDesktop', group: 'notify', type: 'bool' },
  { key: 'notifyMinSeverity', group: 'notify', type: 'severity' },
  { key: 'retentionDays', group: 'data', type: 'number', min: 1, max: 365 },
  { key: 'autoClean', group: 'data', type: 'bool' },
];

const LABELS: Record<string, [string, string, string]> = {
  sampleIntervalSec: ['采样间隔（秒）', '採樣間隔（秒）', 'Sample interval (s)'],
  port: ['服务端口', '服務埠號', 'Server port'],
  language: ['界面语言', '介面語言', 'Language'],
  theme: ['主题', '主題', 'Theme'],
  retentionDays: ['历史保留天数', '歷史保留天數', 'Retention (days)'],
  notifyWeb: ['网页通知', '網頁通知', 'Web notifications'],
  notifySound: ['提示音', '提示音', 'Alert sound'],
  notifyDesktop: ['Windows 桌面通知', 'Windows 桌面通知', 'Windows desktop toast'],
  notifyMinSeverity: ['最低通知级别', '最低通知級別', 'Min notification severity'],
  autoClean: ['自动清理过期数据', '自動清理過期資料', 'Auto-prune old data'],
  autoStart: ['开机自启', '開機自啟', 'Start with Windows'],
  growthThresholdMbps: ['可疑增长阈值 (MB/s)', '可疑成長閾值 (MB/s)', 'Growth threshold (MB/s)'],
};

export default function SettingsPage() {
  const {
    t, lang, settings, health, stats, platform, confirm, toast,
    saveSettings, refreshAll, refreshStatus,
  } = useApp();
  const [draft, setDraft] = useState<Partial<Settings>>({});
  const [saving, setSaving] = useState(false);
  const [busy, setBusy] = useState(false);

  const label = (key: string): string => {
    const row = LABELS[key];
    if (!row) return key;
    return row[lang === 'en' ? 2 : lang === 'zh-TW' ? 1 : 0];
  };

  const valueOf = (key: keyof Settings): unknown => {
    const fromDraft = (draft as Record<string, unknown>)[key];
    if (fromDraft !== undefined) return fromDraft;
    return settings ? (settings as unknown as Record<string, unknown>)[key] : undefined;
  };
  const setField = (key: keyof Settings, value: unknown) =>
    setDraft((prev) => ({ ...prev, [key]: value } as Partial<Settings>));

  const save = async () => {
    if (Object.keys(draft).length === 0) return;
    setSaving(true);
    try {
      const res = await saveSettings(draft);
      setDraft({});
      toast(t('set_saved'), res.note ?? '');
      if (res.requiresRestart) toast(t('set_restart'), res.note ?? '', 'warning');
    } catch (e) {
      toast(t('set_save_failed'), errMsg(e), 'critical');
    } finally { setSaving(false); }
  };

  const collectNow = async () => {
    setBusy(true);
    try { const res = await api.collect(); toast(t('toast_op_ok'), res.error ?? t('set_collect_now')); await refreshStatus(); }
    catch (e) { toast(t('toast_op_fail'), errMsg(e), 'critical'); }
    finally { setBusy(false); }
  };
  const prune = async () => {
    try { const res = await api.prune(); toast(t('toast_op_ok'), `${res.removed}`); await refreshAll(); }
    catch (e) { toast(t('toast_op_fail'), errMsg(e), 'critical'); }
  };
  const clearData = async () => {
    if (!await confirm(t('set_clear_history'), t('confirm_clear_history'))) return;
    try { const res = await api.clearData(); toast(t('toast_op_ok'), `${res.removed}`); await refreshAll(); }
    catch (e) { toast(t('toast_op_fail'), errMsg(e), 'critical'); }
  };

  const renderControl = (spec: FieldSpec) => {
    const value = valueOf(spec.key);
    if (spec.type === 'bool') {
      return <button className={`switch ${value ? 'on' : ''}`} onClick={() => setField(spec.key, !value)} />;
    }
    if (spec.type === 'number') {
      return <input className="input" type="number" style={{ width: 130 }} min={spec.min} max={spec.max}
        value={value === undefined ? '' : String(value)}
        onChange={(e) => setField(spec.key, Number(e.target.value))} />;
    }
    if (spec.type === 'lang') {
      return <select className="select" value={String(value ?? 'zh-CN')}
        onChange={(e) => setField(spec.key, e.target.value)}>
        {LANGUAGES.map((code) => <option key={code} value={code}>{code === 'en' ? 'English' : code === 'zh-TW' ? '繁體中文' : '简体中文'}</option>)}
      </select>;
    }
    if (spec.type === 'theme') {
      return <select className="select" value={String(value ?? 'light')}
        onChange={(e) => setField(spec.key, e.target.value)}>
        <option value="light">{t('theme_light')}</option>
        <option value="dark">{t('theme_dark')}</option>
      </select>;
    }
    return <select className="select" value={String(value ?? 'warning')}
      onChange={(e) => setField(spec.key, e.target.value as Severity)}>
      {(['info', 'warning', 'critical'] as Severity[]).map((s) => <option key={s} value={s}>{t(s)}</option>)}
    </select>;
  };

  return (
    <>
      <div className="page-head">
        <div><h2>{t('set_title')}</h2><p>{t('app_subtitle')}</p></div>
        <div style={{ flex: 1 }} />
        <button className="btn primary" disabled={saving || Object.keys(draft).length === 0} onClick={() => void save()}>
          <Save size={15} />{t('set_save')}{Object.keys(draft).length > 0 && ` (${Object.keys(draft).length})`}
        </button>
      </div>

      <div className="grid cols-2">
        {GROUPS.map((group) => {
          const fields = SCHEMA.filter((f) => f.group === group);
          if (!fields.length) return null;
          return (
            <Card key={group} title={t(`set_group_${group}`)}>
              {fields.map((spec) => (
                <div className="setting-row" key={spec.key}>
                  <div>
                    <div className="label">{label(spec.key)}</div>
                    {spec.hint && <div className="desc">{t(spec.hint)}</div>}
                  </div>
                  {renderControl(spec)}
                </div>
              ))}
            </Card>
          );
        })}
      </div>

      <div className="grid cols-2">
        <Card title={<span className="flex"><AlertTriangle size={15} color="var(--danger)" />{t('set_danger')}</span>}>
          <div className="toolbar">
            <button className="btn" disabled={busy} onClick={() => void collectNow()}><RefreshCw size={14} />{t('set_collect_now')}</button>
            <button className="btn" onClick={() => void prune()}>{t('set_prune')}</button>
            <button className="btn danger" onClick={() => void clearData()}><Trash2 size={14} />{t('set_clear_history')}</button>
          </div>
        </Card>

        <Card title={<span className="flex"><Database size={15} />{t('set_about')}</span>}>
          <div className="kv">
            <span className="k">{t('set_version')}</span><span className="v">v6.0.0</span>
            <span className="k">{t('set_platform')}</span><span className="v">{platform || '--'}</span>
            <span className="k">{t('set_db')}</span><span className="v">vm_manager.sqlite</span>
            <span className="k">{t('set_api')}</span><span className="v">127.0.0.1:{settings?.port ?? 18080}</span>
            <span className="k">{t('health_ok')}</span><span className="v">{health?.ready ? t('yes') : t('no')}</span>
            <span className="k">{t('sse_clients')}</span><span className="v">{health?.sseClients ?? 0}</span>
            <span className="k">{t('hist_samples')}</span><span className="v">{stats?.snapshots.total ?? 0}</span>
            <span className="k">{t('last_update')}</span><span className="v">{stats?.snapshots.newest ? formatTime(stats.snapshots.newest, lang) : '--'}</span>
          </div>
        </Card>
      </div>
    </>
  );
}
