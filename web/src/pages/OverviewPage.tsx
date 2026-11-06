import {
  Area, AreaChart, CartesianGrid, Legend, ResponsiveContainer, Tooltip, XAxis, YAxis,
} from 'recharts';
import { useApp } from '../App';
import { formatTime } from '../i18n';
import { Card, Empty, Ring, SeverityBadge, chartPalette, fmtMb, shortTime, statusColor, tooltipStyle } from '../ui';

export default function OverviewPage() {
  const { status, history, alerts, t, lang, theme, stats } = useApp();
  const palette = chartPalette(theme);
  const memory = status?.memory;
  const gpu = status?.gpu;
  const chartData = history.map((point) => ({
    label: shortTime(point.ts),
    pf: point.pfPct, phys: point.physPct, cpu: point.cpuTotalPct,
    gpu: point.gpuPct ?? undefined,
  }));
  const topProcesses = [...(status?.processes ?? [])]
    .sort((a, b) => b.commitMb - a.commitMb).slice(0, 10);
  const latestAlerts = alerts.slice(0, 4);

  return (
    <>
      <div className="page-head">
        <div><h2>{t('nav_overview')}</h2><p>{t('app_subtitle')}</p></div>
        <div className="spacer" style={{ flex: 1 }} />
        <span className="pill">{t('last_update')}: {status ? formatTime(status.ts, lang) : '--'}</span>
        {stats && <span className="pill">{t('hist_samples')}: {stats.snapshots.total}</span>}
      </div>

      <div className="grid cols-4">
        <Card className="metric-card">
          <Ring pct={memory?.pfPct ?? 0} color={statusColor(memory?.pfPct ?? 0)} text="PF" />
          <div className="meta">
            <div className="label">{t('card_pagefile')}</div>
            <div className="big">{(memory?.pfPct ?? 0).toFixed(1)}%</div>
            <div className="hint">{t('used')} {fmtMb((memory?.totalPfMb ?? 0) - (memory?.availPfMb ?? 0))} / {fmtMb(memory?.totalPfMb ?? 0)}</div>
          </div>
        </Card>
        <Card className="metric-card">
          <Ring pct={memory?.physPct ?? 0} color={statusColor(memory?.physPct ?? 0)} text="RAM" />
          <div className="meta">
            <div className="label">{t('card_phys')}</div>
            <div className="big">{(memory?.physPct ?? 0).toFixed(1)}%</div>
            <div className="hint">{t('available')} {fmtMb(memory?.availPhysMb ?? 0)} / {fmtMb(memory?.totalPhysMb ?? 0)}</div>
          </div>
        </Card>
        <Card className="metric-card">
          <Ring pct={status?.cpuTotalPct ?? 0} color={statusColor(status?.cpuTotalPct ?? 0)} text="CPU" />
          <div className="meta">
            <div className="label">{t('card_cpu')}</div>
            <div className="big">{(status?.cpuTotalPct ?? 0).toFixed(1)}%</div>
            <div className="hint">{t('sys_procs')}: {status?.processCount ?? '--'}</div>
          </div>
        </Card>
        <Card className="metric-card">
          <Ring pct={gpu?.utilizationPct ?? 0} color="var(--primary)" text={gpu?.utilizationPct === null || gpu?.utilizationPct === undefined ? '--' : 'GPU'} />
          <div className="meta">
            <div className="label">{t('card_gpu')}</div>
            <div className="big">{gpu?.utilizationPct === null || gpu?.utilizationPct === undefined ? '--' : `${gpu.utilizationPct.toFixed(1)}%`}</div>
            <div className="hint">{gpu?.vramTotalMb ? `${gpu.vramUsedMb ?? 0} / ${gpu.vramTotalMb} MB` : t('gpu_unavailable')}</div>
          </div>
        </Card>
      </div>

      <div className="grid cols-2" style={{ gridTemplateColumns: 'minmax(0, 1.7fr) minmax(260px, 1fr)' }}>
        <Card title={t('chart_trend')} sub={t('chart_trend_sub')}>
          <div className="chart-box">
            {chartData.length > 0 ? (
              <ResponsiveContainer width="100%" height="100%">
                <AreaChart data={chartData} margin={{ top: 8, right: 12, left: -18, bottom: 0 }}>
                  <defs>
                    <linearGradient id="gradPf" x1="0" y1="0" x2="0" y2="1">
                      <stop offset="0%" stopColor={palette.primary} stopOpacity={0.35} />
                      <stop offset="100%" stopColor={palette.primary} stopOpacity={0} />
                    </linearGradient>
                  </defs>
                  <CartesianGrid strokeDasharray="3 3" stroke={palette.grid} vertical={false} />
                  <XAxis dataKey="label" stroke={palette.axis} tick={{ fontSize: 11 }} minTickGap={28} />
                  <YAxis stroke={palette.axis} tick={{ fontSize: 11 }} domain={[0, 100]} unit="%" />
                  <Tooltip contentStyle={tooltipStyle(theme)} />
                  <Legend wrapperStyle={{ fontSize: 12 }} />
                  <Area type="monotone" dataKey="pf" name={t('metric_pf')} stroke={palette.primary} fill="url(#gradPf)" strokeWidth={2} dot={false} />
                  <Area type="monotone" dataKey="phys" name={t('metric_phys')} stroke={palette.accent} fill="transparent" strokeWidth={2} dot={false} />
                  <Area type="monotone" dataKey="cpu" name={t('metric_cpu')} stroke={palette.warn} fill="transparent" strokeWidth={2} dot={false} />
                  {chartData.some((d) => d.gpu !== undefined) && (
                    <Area type="monotone" dataKey="gpu" name={t('metric_gpu')} stroke={palette.danger} fill="transparent" strokeWidth={1.5} dot={false} />
                  )}
                </AreaChart>
              </ResponsiveContainer>
            ) : <Empty text={t('no_data')} />}
          </div>
        </Card>

        <Card title={t('card_sysinfo')}>
          <div className="kv">
            <span className="k">{t('sys_total_phys')}</span><span className="v">{fmtMb(memory?.totalPhysMb ?? 0)}</span>
            <span className="k">{t('sys_avail_phys')}</span><span className="v">{fmtMb(memory?.availPhysMb ?? 0)}</span>
            <span className="k">{t('sys_total_pf')}</span><span className="v">{fmtMb(memory?.totalPfMb ?? 0)}</span>
            <span className="k">{t('sys_avail_pf')}</span><span className="v">{fmtMb(memory?.availPfMb ?? 0)}</span>
            <span className="k">{t('sys_idle')}</span><span className="v">{Math.round((status?.idleSec ?? 0) / 60)} {t('minutes')}</span>
            <span className="k">{t('sys_procs')}</span><span className="v">{status?.processCount ?? '--'}</span>
            <span className="k">{t('sys_gpu')}</span><span className="v" style={{ fontSize: 11, maxWidth: 160, overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' }}>{gpu?.name ?? '--'}</span>
            <span className="k">{t('vram')}</span><span className="v">{gpu?.vramUsedMb != null ? `${gpu.vramUsedMb} / ${gpu?.vramTotalMb ?? '--'} MB` : '--'}</span>
            <span className="k">{t('sys_uptime')}</span><span className="v">{Math.round((status?.serverUptimeSec ?? 0) / 60)} {t('minutes')}</span>
          </div>
        </Card>
      </div>

      <div className="grid cols-2" style={{ gridTemplateColumns: 'minmax(0, 1.7fr) minmax(260px, 1fr)' }}>
        <Card title={t('proc_top')} sub={`${topProcesses.length} / ${status?.processes.length ?? 0}`}>
          {topProcesses.length ? (
            <div className="table-wrap">
              <table className="table">
                <thead><tr>
                  <th>{t('pid')}</th><th>{t('hdr_name')}</th>
                  <th className="num">{t('hdr_commit')}</th><th className="num">{t('hdr_ws')}</th><th className="num">{t('hdr_cpu')}</th>
                </tr></thead>
                <tbody>{topProcesses.map((proc) => (
                  <tr key={proc.pid}>
                    <td className="mono">{proc.pid}</td>
                    <td>{proc.name}{proc.whitelisted && <span className="badge neutral" style={{ marginLeft: 6 }}>{t('whitelisted')}</span>}</td>
                    <td className="num">{fmtMb(proc.commitMb)}</td>
                    <td className="num">{fmtMb(proc.wsMb)}</td>
                    <td className="num">{proc.cpuPct.toFixed(1)}%</td>
                  </tr>
                ))}</tbody>
              </table>
            </div>
          ) : <Empty text={t('no_data')} />}
        </Card>

        <Card title={t('latest_alerts')} sub={`${alerts.length}`}>
          {latestAlerts.length ? (
            <div style={{ display: 'flex', flexDirection: 'column', gap: 10 }}>
              {latestAlerts.map((alert) => (
                <div key={alert.id} style={{ borderBottom: '1px solid var(--border)', paddingBottom: 9 }}>
                  <div className="flex" style={{ marginBottom: 4 }}>
                    <SeverityBadge severity={alert.severity} lang={lang} />
                    <span className="small muted">{formatTime(alert.ts, lang)}</span>
                  </div>
                  <div className="small">{alert.message}</div>
                </div>
              ))}
            </div>
          ) : <Empty text={t('no_alerts')} />}
        </Card>
      </div>
    </>
  );
}
