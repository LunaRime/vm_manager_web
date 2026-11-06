import { useEffect, useState } from 'react';
import { CartesianGrid, Line, LineChart, ResponsiveContainer, Tooltip, XAxis, YAxis } from 'recharts';
import { RefreshCw, Search, ShieldCheck, Trash2, X } from 'lucide-react';
import { useApp } from '../App';
import { api, ApiError } from '../api';
import { formatTime } from '../i18n';
import type { ProcessDetail, ProcessSample } from '../types';
import { Card, Empty, chartPalette, fmtMb, shortTime, tooltipStyle } from '../ui';

const errMsg = (e: unknown): string =>
  e instanceof ApiError ? e.message : e instanceof Error ? e.message : String(e);

type ActionKind = 'terminate' | 'suspend' | 'resume';

export default function ProcessesPage() {
  const {
    t, lang, theme, status, whitelist, confirm, toast,
    refreshStatus, refreshActions, refreshWhitelist,
  } = useApp();
  const palette = chartPalette(theme);
  const [search, setSearch] = useState('');
  const [sort, setSort] = useState('commitMb');
  const [onlyWhitelisted, setOnlyWhitelisted] = useState(false);
  const [rows, setRows] = useState<ProcessSample[]>([]);
  const [total, setTotal] = useState(0);
  const [busy, setBusy] = useState<number | null>(null);
  const [detailPid, setDetailPid] = useState<number | null>(null);
  const [detail, setDetail] = useState<ProcessDetail | null>(null);
  const [wlPattern, setWlPattern] = useState('');
  const [wlNote, setWlNote] = useState('');

  useEffect(() => {
    let alive = true;
    const timer = window.setTimeout(() => {
      void api.processes(search, sort, 250).then((res) => {
        if (!alive) return;
        setRows(res.processes.filter((p) => !onlyWhitelisted || p.whitelisted));
        setTotal(res.total);
      }).catch(() => { /* keep previous rows */ });
    }, 180);
    return () => { alive = false; window.clearTimeout(timer); };
  }, [search, sort, onlyWhitelisted, status?.ts]);

  useEffect(() => {
    if (detailPid === null) { setDetail(null); return; }
    void api.processDetail(detailPid).then(setDetail).catch(() => setDetail(null));
  }, [detailPid]);

  const runAction = async (kind: ActionKind, proc: ProcessSample) => {
    if (kind !== 'resume') {
      const ok = await confirm(
        kind === 'terminate' ? t('confirm_terminate') : t('confirm_suspend'),
        `${proc.name} · PID ${proc.pid}`,
      );
      if (!ok) return;
    }
    setBusy(proc.pid);
    try {
      const res = kind === 'terminate' ? await api.terminate(proc.pid)
        : kind === 'suspend' ? await api.suspend(proc.pid)
          : await api.resume(proc.pid);
      toast(t(res.ok ? 'toast_op_ok' : 'toast_op_fail'), res.action.detail, res.ok ? 'info' : 'critical');
      await Promise.all([refreshStatus(), refreshActions(), refreshWhitelist()]);
    } catch (e) {
      toast(t('toast_op_fail'), errMsg(e), 'critical');
      void refreshActions();
    } finally {
      setBusy(null);
    }
  };

  const addWhitelist = async () => {
    const pattern = wlPattern.trim();
    if (!pattern) return;
    try {
      await api.addWhitelist(pattern, wlNote.trim());
      setWlPattern(''); setWlNote('');
      toast(t('toast_op_ok'), pattern);
      await Promise.all([refreshWhitelist(), refreshStatus()]);
    } catch (e) {
      toast(t('toast_op_fail'), errMsg(e), 'critical');
    }
  };

  const removeWhitelist = async (id: number) => {
    try {
      await api.deleteWhitelist(id);
      await Promise.all([refreshWhitelist(), refreshStatus()]);
    } catch (e) {
      toast(t('toast_op_fail'), errMsg(e), 'critical');
    }
  };

  const chartData = (detail?.history ?? []).map((point) => ({
    label: shortTime(point.ts), commit: point.commitMb, ws: point.wsMb,
  }));

  return (
    <>
      <div className="page-head">
        <div><h2>{t('proc_title')}</h2><p>{t('proc_subtitle')}</p></div>
      </div>

      <Card>
        <div className="toolbar">
          <div className="input" style={{ display: 'flex', alignItems: 'center', gap: 7, padding: '0 10px', minWidth: 240 }}>
            <Search size={15} color="var(--muted)" />
            <input className="input" style={{ border: 0, padding: '8px 0', boxShadow: 'none', width: '100%' }}
              placeholder={t('search_placeholder')} value={search} onChange={(e) => setSearch(e.target.value)} />
          </div>
          <select className="select" value={sort} onChange={(e) => setSort(e.target.value)}>
            <option value="commitMb">{t('hdr_commit')}</option>
            <option value="cpuPct">{t('hdr_cpu')}</option>
            <option value="wsMb">{t('hdr_ws')}</option>
            <option value="growthMbps">{t('hdr_growth')}</option>
            <option value="name">{t('hdr_name')}</option>
            <option value="pid">{t('pid')}</option>
          </select>
          <label className="pill" style={{ cursor: 'pointer' }}>
            <button className={`switch ${onlyWhitelisted ? 'on' : ''}`} onClick={() => setOnlyWhitelisted(!onlyWhitelisted)} />
            {t('whitelist_only')}
          </label>
          <div className="spacer" />
          <span className="pill">{t('total')} {rows.length} / {total}</span>
          <button className="btn icon" title={t('refresh')} onClick={() => void refreshStatus()}><RefreshCw size={15} /></button>
        </div>

        <div className="hr" />
        {rows.length ? (
          <div className="table-wrap" style={{ maxHeight: 560 }}>
            <table className="table">
              <thead><tr>
                <th>{t('pid')}</th><th>{t('hdr_name')}</th>
                <th className="num">{t('hdr_commit')}</th><th className="num">{t('hdr_ws')}</th>
                <th className="num">{t('hdr_cpu')}</th><th className="num">{t('hdr_growth')}</th>
                <th>{t('hdr_status')}</th><th className="right">{t('actions')}</th>
              </tr></thead>
              <tbody>{rows.map((proc) => (
                <tr key={proc.pid}>
                  <td className="mono">{proc.pid}</td>
                  <td>{proc.name}</td>
                  <td className="num">{fmtMb(proc.commitMb)}</td>
                  <td className="num">{fmtMb(proc.wsMb)}</td>
                  <td className="num">{proc.cpuPct.toFixed(1)}%</td>
                  <td className="num">{proc.growthMbps.toFixed(1)} MB/s</td>
                  <td>
                    {proc.whitelisted && <span className="badge ok">{t('whitelisted')}</span>}
                    {proc.suspiciousCount >= 3 && <span className="badge critical" style={{ marginLeft: 5 }}>{t('suspicious')}</span>}
                    {!proc.whitelisted && proc.suspiciousCount < 3 && <span className="badge neutral">{t('normal')}</span>}
                  </td>
                  <td>
                    <div className="flex" style={{ justifyContent: 'flex-end' }}>
                      <button className="btn sm" onClick={() => setDetailPid(proc.pid)}>{t('btn_detail')}</button>
                      <button className="btn sm" disabled={busy === proc.pid} onClick={() => void runAction('suspend', proc)}>{t('btn_suspend')}</button>
                      <button className="btn sm" disabled={busy === proc.pid} onClick={() => void runAction('resume', proc)}>{t('btn_resume')}</button>
                      <button className="btn sm danger" disabled={busy === proc.pid || proc.whitelisted}
                        onClick={() => void runAction('terminate', proc)}>{t('btn_terminate')}</button>
                    </div>
                  </td>
                </tr>
              ))}</tbody>
            </table>
          </div>
        ) : <Empty text={t('no_process')} />}
      </Card>

      <Card title={<span className="flex"><ShieldCheck size={15} />{t('whitelist_title')}</span>} sub={`${whitelist.length}`}>
        <div className="toolbar" style={{ marginBottom: 14 }}>
          <input className="input" style={{ minWidth: 260 }} placeholder={t('whitelist_pattern')}
            value={wlPattern} onChange={(e) => setWlPattern(e.target.value)}
            onKeyDown={(e) => { if (e.key === 'Enter') void addWhitelist(); }} />
          <input className="input" style={{ minWidth: 180 }} placeholder={t('whitelist_note')}
            value={wlNote} onChange={(e) => setWlNote(e.target.value)} />
          <button className="btn primary" onClick={() => void addWhitelist()}>{t('whitelist_add')}</button>
        </div>
        {whitelist.length ? (
          <div className="flex wrap" style={{ gap: 8 }}>
            {whitelist.map((entry) => (
              <span key={entry.id} className="pill" title={entry.note}>
                <ShieldCheck size={13} color="var(--ok)" />{entry.pattern}
                <button className="btn sm icon" style={{ padding: 2, border: 0 }} title={t('delete')}
                  onClick={() => void removeWhitelist(entry.id)}><Trash2 size={13} /></button>
              </span>
            ))}
          </div>
        ) : <Empty text={t('whitelist_empty')} />}
      </Card>

      {detailPid !== null && (
        <div className="overlay" style={{ justifyContent: 'flex-end', padding: 0 }} onClick={() => setDetailPid(null)}>
          <div className="drawer" onClick={(e) => e.stopPropagation()}>
            <div className="drawer-head">
              <h3>{detail?.process?.name ?? `PID ${detailPid}`}</h3>
              <button className="btn icon" onClick={() => setDetailPid(null)}><X size={16} /></button>
            </div>
            {detail?.process ? (
              <>
                <div className="kv">
                  <span className="k">{t('pid')}</span><span className="v">{detail.process.pid}</span>
                  <span className="k">{t('hdr_commit')}</span><span className="v">{fmtMb(detail.process.commitMb)}</span>
                  <span className="k">{t('hdr_ws')}</span><span className="v">{fmtMb(detail.process.wsMb)}</span>
                  <span className="k">{t('hdr_cpu')}</span><span className="v">{detail.process.cpuPct.toFixed(2)}%</span>
                  <span className="k">{t('hdr_peak')}</span><span className="v">{detail.process.peakGrowthMbps.toFixed(1)} MB/s</span>
                  <span className="k">{t('proc_created')}</span><span className="v">{detail.process.createdAt ? formatTime(detail.process.createdAt, lang) : '--'}</span>
                </div>
                <div className="hr" />
                <div className="small muted" style={{ wordBreak: 'break-all' }}>{t('proc_path')}: {detail.process.path ?? '--'}</div>
                <div className="hr" />
                <h4 style={{ margin: '0 0 10px', fontSize: 13 }}>{t('proc_history')}</h4>
                <div className="chart-box small">
                  {chartData.length > 1 ? (
                    <ResponsiveContainer width="100%" height="100%">
                      <LineChart data={chartData} margin={{ top: 5, right: 8, left: -20, bottom: 0 }}>
                        <CartesianGrid strokeDasharray="3 3" stroke={palette.grid} vertical={false} />
                        <XAxis dataKey="label" tick={{ fontSize: 10 }} stroke={palette.axis} minTickGap={24} />
                        <YAxis tick={{ fontSize: 10 }} stroke={palette.axis} />
                        <Tooltip contentStyle={tooltipStyle(theme)} />
                        <Line type="monotone" dataKey="commit" name={t('hdr_commit')} stroke={palette.primary} dot={false} strokeWidth={2} />
                        <Line type="monotone" dataKey="ws" name={t('hdr_ws')} stroke={palette.accent} dot={false} strokeWidth={2} />
                      </LineChart>
                    </ResponsiveContainer>
                  ) : <Empty text={t('no_data')} />}
                </div>
              </>
            ) : <Empty text={t('loading')} />}
          </div>
        </div>
      )}
    </>
  );
}
