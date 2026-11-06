import { useState } from 'react';
import {
  Bar, BarChart, CartesianGrid, Legend, Line, LineChart,
  ResponsiveContainer, Tooltip, XAxis, YAxis,
} from 'recharts';
import { Download } from 'lucide-react';
import { useApp } from '../App';
import { ApiError, downloadExport } from '../api';
import type { ExportFormat, ExportKind } from '../types';
import { Card, Empty, chartPalette, shortTime, tooltipStyle } from '../ui';

const errMsg = (e: unknown): string =>
  e instanceof ApiError ? e.message : e instanceof Error ? e.message : String(e);

const RANGES = ['1h', '24h', '7d', '30d', '1y'];
const AGG_RANGES = ['day', 'week', 'month', 'year'];
const KINDS: ExportKind[] = ['snapshots', 'alerts', 'processes', 'actions'];

export default function HistoryPage() {
  const { t, lang, theme, history, historyRange, setHistoryRange, aggregated, aggRange, setAggRange, toast } = useApp();
  const palette = chartPalette(theme);
  const [metrics, setMetrics] = useState({ pf: true, phys: true, cpu: true, gpu: false });
  const [kind, setKind] = useState<ExportKind>('snapshots');
  const [format, setFormat] = useState<ExportFormat>('csv');
  const [exporting, setExporting] = useState(false);

  const trendData = history.map((p) => ({
    label: shortTime(p.ts), pf: p.pfPct, phys: p.physPct, cpu: p.cpuTotalPct, gpu: p.gpuPct,
  }));
  const aggData = aggregated.map((p) => ({
    label: new Date(p.ts).toLocaleDateString(lang === 'en' ? 'en-US' : lang),
    avgPf: Number(p.avgPf.toFixed(1)), maxPf: Number(p.maxPf.toFixed(1)),
    avgPhys: Number(p.avgPhys.toFixed(1)), maxCpu: Number(p.maxCpu.toFixed(1)),
  }));

  const average = (values: number[]) => values.length ? values.reduce((a, b) => a + b, 0) / values.length : 0;
  const maximum = (values: number[]) => values.length ? Math.max(...values) : 0;
  const pfValues = history.map((p) => p.pfPct);
  const cpuValues = history.map((p) => p.cpuTotalPct);

  const doExport = async () => {
    setExporting(true);
    try {
      await downloadExport(kind, format, historyRange);
      toast(t('toast_op_ok'), `${kind}.${format}`);
    } catch (e) {
      toast(t('toast_op_fail'), errMsg(e), 'critical');
    } finally {
      setExporting(false);
    }
  };

  const metricToggles: Array<{ key: keyof typeof metrics; label: string; color: string }> = [
    { key: 'pf', label: t('metric_pf'), color: palette.primary },
    { key: 'phys', label: t('metric_phys'), color: palette.accent },
    { key: 'cpu', label: t('metric_cpu'), color: palette.warn },
    { key: 'gpu', label: t('metric_gpu'), color: palette.danger },
  ];

  return (
    <>
      <div className="page-head">
        <div><h2>{t('hist_title')}</h2><p>{t('hist_range')}</p></div>
        <div style={{ flex: 1 }} />
        <div className="segmented">
          {RANGES.map((range) => (
            <button key={range} className={historyRange === range ? 'active' : ''} onClick={() => setHistoryRange(range)}>
              {range}
            </button>
          ))}
        </div>
      </div>

      <div className="grid cols-4">
        <div className="stat"><div className="label">{t('hist_samples')}</div><div className="value">{history.length}</div></div>
        <div className="stat"><div className="label">{t('hist_avg_pf')}</div><div className="value">{average(pfValues).toFixed(1)}%</div></div>
        <div className="stat"><div className="label">{t('hist_max_pf')}</div><div className="value">{maximum(pfValues).toFixed(1)}%</div></div>
        <div className="stat"><div className="label">{t('hist_avg_cpu')} / {t('hist_max_cpu')}</div><div className="value">{average(cpuValues).toFixed(1)} / {maximum(cpuValues).toFixed(1)}%</div></div>
      </div>

      <Card title={t('hist_trend')} sub={historyRange}>
        <div className="toolbar" style={{ marginBottom: 12 }}>
          <div className="flex wrap">
            {metricToggles.map((item) => (
              <button key={item.key} className={`btn sm ${metrics[item.key] ? 'primary' : ''}`}
                style={metrics[item.key] ? { background: item.color, borderColor: item.color } : undefined}
                onClick={() => setMetrics((prev) => ({ ...prev, [item.key]: !prev[item.key] }))}>
                {item.label}
              </button>
            ))}
          </div>
        </div>
        <div className="chart-box" style={{ height: 290 }}>
          {trendData.length > 0 ? (
            <ResponsiveContainer width="100%" height="100%">
              <LineChart data={trendData} margin={{ top: 8, right: 12, left: -18, bottom: 0 }}>
                <CartesianGrid strokeDasharray="3 3" stroke={palette.grid} vertical={false} />
                <XAxis dataKey="label" tick={{ fontSize: 11 }} stroke={palette.axis} minTickGap={30} />
                <YAxis tick={{ fontSize: 11 }} stroke={palette.axis} domain={[0, 100]} unit="%" />
                <Tooltip contentStyle={tooltipStyle(theme)} />
                <Legend wrapperStyle={{ fontSize: 12 }} />
                {metrics.pf && <Line type="monotone" dataKey="pf" name={t('metric_pf')} stroke={palette.primary} dot={false} strokeWidth={2} />}
                {metrics.phys && <Line type="monotone" dataKey="phys" name={t('metric_phys')} stroke={palette.accent} dot={false} strokeWidth={2} />}
                {metrics.cpu && <Line type="monotone" dataKey="cpu" name={t('metric_cpu')} stroke={palette.warn} dot={false} strokeWidth={2} />}
                {metrics.gpu && <Line type="monotone" dataKey="gpu" name={t('metric_gpu')} stroke={palette.danger} dot={false} strokeWidth={2} connectNulls />}
              </LineChart>
            </ResponsiveContainer>
          ) : <Empty text={t('no_data')} />}
        </div>
      </Card>

      <div className="grid cols-2" style={{ gridTemplateColumns: 'minmax(0, 1.6fr) minmax(280px, 1fr)' }}>
        <Card title={t('hist_agg')} sub={aggRange}>
          <div className="toolbar" style={{ marginBottom: 12 }}>
            <div className="segmented">
              {AGG_RANGES.map((range) => (
                <button key={range} className={aggRange === range ? 'active' : ''} onClick={() => setAggRange(range)}>
                  {t(`agg_${range}`)}
                </button>
              ))}
            </div>
          </div>
          <div className="chart-box small">
            {aggData.length > 0 ? (
              <ResponsiveContainer width="100%" height="100%">
                <BarChart data={aggData} margin={{ top: 6, right: 10, left: -18, bottom: 0 }}>
                  <CartesianGrid strokeDasharray="3 3" stroke={palette.grid} vertical={false} />
                  <XAxis dataKey="label" tick={{ fontSize: 10 }} stroke={palette.axis} minTickGap={18} />
                  <YAxis tick={{ fontSize: 10 }} stroke={palette.axis} domain={[0, 100]} />
                  <Tooltip contentStyle={tooltipStyle(theme)} />
                  <Legend wrapperStyle={{ fontSize: 11 }} />
                  <Bar dataKey="avgPf" name={t('hist_avg_pf')} fill={palette.primary} radius={[4, 4, 0, 0]} />
                  <Bar dataKey="maxPf" name={t('hist_max_pf')} fill={palette.warn} radius={[4, 4, 0, 0]} />
                  <Bar dataKey="avgPhys" name={t('metric_phys')} fill={palette.accent} radius={[4, 4, 0, 0]} />
                </BarChart>
              </ResponsiveContainer>
            ) : <Empty text={t('no_data')} />}
          </div>
        </Card>

        <Card title={t('export_title')}>
          <div className="field" style={{ marginBottom: 12 }}>
            <label>{t('export_kind')}</label>
            <select className="select" value={kind} onChange={(e) => setKind(e.target.value as ExportKind)}>
              {KINDS.map((item) => <option key={item} value={item}>{t(`kind_${item}`)}</option>)}
            </select>
          </div>
          <div className="field" style={{ marginBottom: 16 }}>
            <label>{t('export_format')}</label>
            <div className="segmented">
              {(['csv', 'json'] as ExportFormat[]).map((item) => (
                <button key={item} className={format === item ? 'active' : ''} onClick={() => setFormat(item)}>
                  {item.toUpperCase()}
                </button>
              ))}
            </div>
          </div>
          <button className="btn primary" disabled={exporting} onClick={() => void doExport()}>
            <Download size={15} />{t('export_btn')}
          </button>
          <div className="hr" />
          <div className="small muted">{t('hist_range')}: {historyRange} · {t('rows')}: {history.length}</div>
        </Card>
      </div>
    </>
  );
}
