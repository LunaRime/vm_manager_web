import { useState } from 'react';
import { BellRing, Pencil, Plus, Trash2, X } from 'lucide-react';
import { useApp } from '../App';
import { api, ApiError } from '../api';
import { formatTime } from '../i18n';
import type { AlertMetric, AlertRule, Severity } from '../types';
import { Card, Empty, SeverityBadge } from '../ui';

const errMsg = (e: unknown): string =>
  e instanceof ApiError ? e.message : e instanceof Error ? e.message : String(e);

const METRICS: AlertMetric[] = ['cpu', 'commit', 'gpu', 'pagefile', 'phys', 'growth'];
const SEVERITIES: Severity[] = ['info', 'warning', 'critical'];

interface RuleForm {
  id?: string;
  name: string; metric: AlertMetric; op: 'gt' | 'lt'; threshold: number;
  consecutive: number; severity: Severity; enabled: boolean; cooldownSec: number;
}

const EMPTY_FORM: RuleForm = {
  name: '', metric: 'cpu', op: 'gt', threshold: 50, consecutive: 3,
  severity: 'warning', enabled: true, cooldownSec: 300,
};

const toPayload = (rule: AlertRule) => ({
  id: rule.id, name: rule.name, metric: rule.metric, op: rule.op,
  threshold: rule.threshold, consecutive: rule.consecutive,
  severity: rule.severity, enabled: rule.enabled, cooldownSec: rule.cooldownSec,
});

export default function AlertsPage() {
  const { t, lang, alerts, rules, confirm, toast, refreshAlerts, refreshRules } = useApp();
  const [filter, setFilter] = useState<'all' | 'unacked'>('all');
  const [severity, setSeverity] = useState<'all' | Severity>('all');
  const [form, setForm] = useState<RuleForm | null>(null);
  const [saving, setSaving] = useState(false);

  const visible = alerts.filter((a) =>
    (filter === 'all' || !a.acknowledged) && (severity === 'all' || a.severity === severity));

  const ack = async (id: number) => {
    try { await api.ackAlert(id); await refreshAlerts(); }
    catch (e) { toast(t('toast_op_fail'), errMsg(e), 'critical'); }
  };
  const ackAll = async () => {
    try { const res = await api.ackAllAlerts(); toast(t('toast_op_ok'), `${res.count}`); await refreshAlerts(); }
    catch (e) { toast(t('toast_op_fail'), errMsg(e), 'critical'); }
  };
  const clearAlerts = async () => {
    if (!await confirm(t('alert_clear'), t('confirm_clear_alerts'))) return;
    try { const res = await api.clearAlerts(); toast(t('toast_op_ok'), `${res.count}`); await refreshAlerts(); }
    catch (e) { toast(t('toast_op_fail'), errMsg(e), 'critical'); }
  };
  const saveForm = async () => {
    if (!form || !form.name.trim()) return;
    setSaving(true);
    try {
      await api.saveRule({ ...form, name: form.name.trim() });
      toast(t('toast_op_ok'), form.name);
      setForm(null);
      await refreshRules();
    } catch (e) {
      toast(t('toast_op_fail'), errMsg(e), 'critical');
    } finally { setSaving(false); }
  };
  const toggleRule = async (rule: AlertRule) => {
    try { await api.saveRule({ ...toPayload(rule), enabled: !rule.enabled }); await refreshRules(); }
    catch (e) { toast(t('toast_op_fail'), errMsg(e), 'critical'); }
  };
  const deleteRule = async (rule: AlertRule) => {
    if (!await confirm(t('rule_delete'), t('confirm_delete_rule'))) return;
    try { await api.deleteRule(rule.id); await refreshRules(); }
    catch (e) { toast(t('toast_op_fail'), errMsg(e), 'critical'); }
  };

  return (
    <>
      <div className="page-head">
        <div><h2>{t('alert_title')}</h2><p>{t('alert_filters')}</p></div>
        <div style={{ flex: 1 }} />
        <button className="btn" onClick={() => void ackAll()}><BellRing size={14} />{t('alert_ack_all')}</button>
        <button className="btn danger" onClick={() => void clearAlerts()}><Trash2 size={14} />{t('alert_clear')}</button>
      </div>

      <Card title={t('alert_title')} sub={`${visible.length} / ${alerts.length}`}>
        <div className="toolbar" style={{ marginBottom: 14 }}>
          <div className="segmented">
            <button className={filter === 'all' ? 'active' : ''} onClick={() => setFilter('all')}>{t('alert_all')}</button>
            <button className={filter === 'unacked' ? 'active' : ''} onClick={() => setFilter('unacked')}>{t('alert_unacked')}</button>
          </div>
          <select className="select" value={severity} onChange={(e) => setSeverity(e.target.value as 'all' | Severity)}>
            <option value="all">{t('severity')}: {t('all')}</option>
            {SEVERITIES.map((s) => <option key={s} value={s}>{t(s)}</option>)}
          </select>
        </div>
        {visible.length ? (
          <div className="table-wrap">
            <table className="table">
              <thead><tr>
                <th>{t('time')}</th><th>{t('severity')}</th><th>{t('status')}</th>
                <th>{t('name')}</th><th>{t('pid')}</th><th>{t('value')}</th><th className="right">{t('actions')}</th>
              </tr></thead>
              <tbody>{visible.map((alert) => (
                <tr key={alert.id}>
                  <td className="mono">{formatTime(alert.ts, lang)}</td>
                  <td><SeverityBadge severity={alert.severity} lang={lang} /></td>
                  <td>{alert.acknowledged ? <span className="badge neutral">{t('ack')} ✓</span> : <span className="badge warning">{t('alert_unacked')}</span>}</td>
                  <td>{alert.message}</td>
                  <td className="mono">{alert.pid ?? '--'}</td>
                  <td className="num">{alert.value.toFixed(1)} / {alert.threshold}</td>
                  <td className="right">
                    {!alert.acknowledged && <button className="btn sm" onClick={() => void ack(alert.id)}>{t('alert_ack')}</button>}
                  </td>
                </tr>
              ))}</tbody>
            </table>
          </div>
        ) : <Empty text={t('alert_empty')} />}
      </Card>

      <Card title={t('rule_title')} sub={`${rules.length}`}>
        <div className="toolbar" style={{ marginBottom: 14 }}>
          <button className="btn primary" onClick={() => setForm({ ...EMPTY_FORM })}><Plus size={14} />{t('rule_new')}</button>
        </div>
        {rules.length ? (
          <div className="table-wrap">
            <table className="table">
              <thead><tr>
                <th>{t('rule_name')}</th><th>{t('rule_metric')}</th><th>{t('rule_op')}</th>
                <th className="num">{t('threshold')}</th><th className="num">{t('rule_consecutive')}</th>
                <th>{t('severity')}</th><th className="num">{t('rule_cooldown')}</th>
                <th>{t('status')}</th><th className="right">{t('actions')}</th>
              </tr></thead>
              <tbody>{rules.map((rule) => (
                <tr key={rule.id}>
                  <td>{rule.name}</td>
                  <td>{t(`rule_metric_${rule.metric}`)}</td>
                  <td>{rule.op === 'gt' ? '>' : '<'}</td>
                  <td className="num">{rule.threshold}</td>
                  <td className="num">{rule.consecutive}</td>
                  <td><SeverityBadge severity={rule.severity} lang={lang} /></td>
                  <td className="num">{rule.cooldownSec}</td>
                  <td><button className={`switch ${rule.enabled ? 'on' : ''}`} onClick={() => void toggleRule(rule)} /></td>
                  <td>
                    <div className="flex" style={{ justifyContent: 'flex-end' }}>
                      <button className="btn sm icon" title={t('edit')} onClick={() => setForm({ ...toPayload(rule) })}><Pencil size={13} /></button>
                      <button className="btn sm icon danger" title={t('delete')} onClick={() => void deleteRule(rule)}><Trash2 size={13} /></button>
                    </div>
                  </td>
                </tr>
              ))}</tbody>
            </table>
          </div>
        ) : <Empty text={t('rule_empty')} />}
      </Card>

      {form && (
        <div className="overlay" onClick={() => setForm(null)}>
          <div className="modal" style={{ width: 'min(560px, 94vw)' }} onClick={(e) => e.stopPropagation()}>
            <div className="drawer-head">
              <h3>{form.id ? t('rule_edit') : t('rule_new')}</h3>
              <button className="btn icon" onClick={() => setForm(null)}><X size={16} /></button>
            </div>
            <div className="form-grid">
              <div className="field full"><label>{t('rule_name')}</label>
                <input className="input" value={form.name} onChange={(e) => setForm({ ...form, name: e.target.value })} />
              </div>
              <div className="field"><label>{t('rule_metric')}</label>
                <select className="select" value={form.metric} onChange={(e) => setForm({ ...form, metric: e.target.value as AlertMetric })}>
                  {METRICS.map((m) => <option key={m} value={m}>{t(`rule_metric_${m}`)}</option>)}
                </select>
              </div>
              <div className="field"><label>{t('rule_op')}</label>
                <select className="select" value={form.op} onChange={(e) => setForm({ ...form, op: e.target.value as 'gt' | 'lt' })}>
                  <option value="gt">{t('op_gt')} (&gt;)</option>
                  <option value="lt">{t('op_lt')} (&lt;)</option>
                </select>
              </div>
              <div className="field"><label>{t('threshold')}</label>
                <input className="input" type="number" min={0} value={form.threshold}
                  onChange={(e) => setForm({ ...form, threshold: Number(e.target.value) })} />
              </div>
              <div className="field"><label>{t('rule_consecutive')}</label>
                <input className="input" type="number" min={1} max={60} value={form.consecutive}
                  onChange={(e) => setForm({ ...form, consecutive: Number(e.target.value) })} />
              </div>
              <div className="field"><label>{t('rule_severity')}</label>
                <select className="select" value={form.severity} onChange={(e) => setForm({ ...form, severity: e.target.value as Severity })}>
                  {SEVERITIES.map((s) => <option key={s} value={s}>{t(s)}</option>)}
                </select>
              </div>
              <div className="field"><label>{t('rule_cooldown')}</label>
                <input className="input" type="number" min={0} value={form.cooldownSec}
                  onChange={(e) => setForm({ ...form, cooldownSec: Number(e.target.value) })} />
              </div>
              <div className="field full"><label>{t('rule_enabled')}</label>
                <button className={`switch ${form.enabled ? 'on' : ''}`}
                  onClick={() => setForm({ ...form, enabled: !form.enabled })} />
              </div>
            </div>
            <div className="hr" />
            <div className="modal-actions">
              <button className="btn" onClick={() => setForm(null)}>{t('cancel')}</button>
              <button className="btn primary" disabled={saving || !form.name.trim()} onClick={() => void saveForm()}>{t('save')}</button>
            </div>
          </div>
        </div>
      )}
    </>
  );
}
