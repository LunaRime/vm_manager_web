import type { CSSProperties, ReactNode } from 'react';
import { Activity } from 'lucide-react';
import { t } from './i18n';
import type { Language, Severity, Theme } from './types';

export function Card({ title, sub, children, className = '', style }: {
  title?: ReactNode; sub?: ReactNode; children: ReactNode; className?: string; style?: CSSProperties;
}) {
  return (
    <section className={`card ${className}`} style={style}>
      {title && <h3 className="card-title"><Activity size={15} color="var(--primary)" />{title}{sub && <span className="sub">{sub}</span>}</h3>}
      {children}
    </section>
  );
}

export function Ring({ pct, color, text }: { pct: number; color?: string; text?: ReactNode }) {
  const safe = Math.max(0, Math.min(100, Number.isFinite(pct) ? pct : 0));
  return (
    <div className="ring" style={{ '--pct': safe, '--ring-color': color ?? 'var(--primary)' } as CSSProperties}>
      <span>{text ?? `${safe.toFixed(0)}%`}</span>
    </div>
  );
}

export function statusColor(pct: number): string {
  return pct >= 85 ? 'var(--danger)' : pct >= 65 ? 'var(--warn)' : 'var(--ok)';
}

export function SeverityBadge({ severity, lang }: { severity: Severity; lang: Language }) {
  return <span className={`badge ${severity}`}>{t(severity, lang)}</span>;
}

export function Empty({ text }: { text: string }) {
  return <div className="empty">{text}</div>;
}

export function shortTime(ts: number): string {
  if (!ts) return '--:--';
  return new Date(ts).toLocaleTimeString([], { hour: '2-digit', minute: '2-digit', hour12: false });
}

export function chartPalette(theme: Theme) {
  const dark = theme === 'dark';
  return {
    grid: dark ? '#28374c' : '#e8edf4',
    axis: dark ? '#93a2b8' : '#8a94a5',
    tooltipBg: dark ? '#151f2e' : '#ffffff',
    tooltipBorder: dark ? '#28374c' : '#e3e8f0',
    text: dark ? '#e7edf7' : '#1b2434',
    primary: dark ? '#5b8cff' : '#2f6fed',
    accent: dark ? '#2cc7bd' : '#0b9a95',
    warn: dark ? '#f0b23c' : '#df9309',
    danger: dark ? '#ff6b6f' : '#e5484d',
    ok: dark ? '#35c48b' : '#17a673',
  };
}

export function tooltipStyle(theme: Theme): CSSProperties {
  const p = chartPalette(theme);
  return {
    background: p.tooltipBg, border: `1px solid ${p.tooltipBorder}`,
    borderRadius: 10, color: p.text, fontSize: 12,
    boxShadow: '0 8px 24px rgba(0,0,0,.12)',
  };
}

export function fmtMb(mb: number): string {
  if (!Number.isFinite(mb)) return '--';
  return mb >= 1024 ? `${(mb / 1024).toFixed(2)} GB` : `${mb.toFixed(1)} MB`;
}
