import Database from 'better-sqlite3';
import { mkdirSync } from 'node:fs';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { DEFAULT_SETTINGS } from './config.js';
import type {
  ActionLog, AlertEvent, AlertRule, HistoryPoint, AggregatedPoint,
  ProcessSample, Settings, Severity, Snapshot, WhitelistEntry,
} from './types.js';

export const PROJECT_ROOT = fileURLToPath(new URL('../../', import.meta.url));
export const WEB_DIST = resolve(PROJECT_ROOT, 'web', 'dist');

const DB_PATH = process.env.VM_DB_PATH
  ? resolve(process.env.VM_DB_PATH)
  : resolve(PROJECT_ROOT, 'vm_manager.sqlite');

mkdirSync(dirname(DB_PATH), { recursive: true });

const db = new Database(DB_PATH);
db.pragma('journal_mode = WAL');
db.pragma('busy_timeout = 5000');

db.exec(`
CREATE TABLE IF NOT EXISTS settings (
  key TEXT PRIMARY KEY,
  value TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS snapshots (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  ts INTEGER NOT NULL,
  total_phys_mb REAL NOT NULL, avail_phys_mb REAL NOT NULL, phys_pct REAL NOT NULL,
  total_pf_mb REAL NOT NULL, avail_pf_mb REAL NOT NULL, pf_pct REAL NOT NULL,
  cpu_total_pct REAL NOT NULL DEFAULT 0,
  gpu_pct REAL, gpu_vram_used_mb REAL, gpu_vram_total_mb REAL,
  idle_sec INTEGER NOT NULL DEFAULT 0,
  proc_count INTEGER NOT NULL DEFAULT 0,
  demo INTEGER NOT NULL DEFAULT 0
);
CREATE INDEX IF NOT EXISTS idx_snapshots_ts ON snapshots(ts);
CREATE TABLE IF NOT EXISTS proc_samples (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  ts INTEGER NOT NULL,
  pid INTEGER NOT NULL, name TEXT NOT NULL,
  commit_mb REAL NOT NULL, ws_mb REAL NOT NULL, cpu_pct REAL NOT NULL,
  growth_mbps REAL NOT NULL DEFAULT 0,
  peak_growth_mbps REAL NOT NULL DEFAULT 0,
  suspicious_count INTEGER NOT NULL DEFAULT 0,
  path TEXT, created_at INTEGER
);
CREATE INDEX IF NOT EXISTS idx_proc_ts ON proc_samples(ts);
CREATE TABLE IF NOT EXISTS alerts (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  ts INTEGER NOT NULL,
  rule_id TEXT NOT NULL, type TEXT NOT NULL, severity TEXT NOT NULL,
  pid INTEGER, proc_name TEXT, value REAL NOT NULL, threshold REAL NOT NULL,
  message TEXT NOT NULL, acknowledged INTEGER NOT NULL DEFAULT 0
);
CREATE INDEX IF NOT EXISTS idx_alerts_ts ON alerts(ts);
CREATE TABLE IF NOT EXISTS rules (
  id TEXT PRIMARY KEY,
  name TEXT NOT NULL, metric TEXT NOT NULL, op TEXT NOT NULL,
  threshold REAL NOT NULL, consecutive INTEGER NOT NULL,
  severity TEXT NOT NULL, enabled INTEGER NOT NULL,
  cooldown_sec INTEGER NOT NULL, created_at INTEGER NOT NULL
);
CREATE TABLE IF NOT EXISTS whitelist (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  pattern TEXT NOT NULL UNIQUE, note TEXT NOT NULL DEFAULT '', created_at INTEGER NOT NULL
);
CREATE TABLE IF NOT EXISTS actions (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  ts INTEGER NOT NULL, kind TEXT NOT NULL, detail TEXT NOT NULL, ok INTEGER NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_actions_ts ON actions(ts);
`);

type Row = Record<string, unknown>;
const num = (v: unknown): number => (typeof v === 'number' ? v : Number(v ?? 0));
const str = (v: unknown): string => (v === null || v === undefined ? '' : String(v));
const bool = (v: unknown): boolean => num(v) !== 0;

/* ------------------------------- settings -------------------------------- */

export function loadSettings(): Settings {
  const rows = db.prepare('SELECT key, value FROM settings').all() as Array<{ key: string; value: string }>;
  const stored: Record<string, unknown> = {};
  for (const row of rows) {
    try { stored[row.key] = JSON.parse(row.value); } catch { /* ignore corrupt entry */ }
  }
  return { ...DEFAULT_SETTINGS, ...stored } as Settings;
}

export function saveSettings(settings: Settings): void {
  const stmt = db.prepare(`INSERT INTO settings (key, value) VALUES (?, ?)
    ON CONFLICT(key) DO UPDATE SET value = excluded.value`);
  db.transaction(() => {
    for (const [key, value] of Object.entries(settings)) stmt.run(key, JSON.stringify(value));
  })();
}

/* ------------------------------ snapshots -------------------------------- */

export function insertSnapshot(s: Snapshot): void {
  db.prepare(`INSERT INTO snapshots
    (ts, total_phys_mb, avail_phys_mb, phys_pct, total_pf_mb, avail_pf_mb, pf_pct,
     cpu_total_pct, gpu_pct, gpu_vram_used_mb, gpu_vram_total_mb, idle_sec, proc_count, demo)
    VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)`).run(
    s.ts, s.memory.totalPhysMb, s.memory.availPhysMb, s.memory.physPct,
    s.memory.totalPfMb, s.memory.availPfMb, s.memory.pfPct,
    s.cpuTotalPct, s.gpu.utilizationPct, s.gpu.vramUsedMb, s.gpu.vramTotalMb,
    s.idleSec, s.processCount, s.demo ? 1 : 0,
  );
}

const insertProcStmt = db.prepare(`INSERT INTO proc_samples
  (ts, pid, name, commit_mb, ws_mb, cpu_pct, growth_mbps, peak_growth_mbps, suspicious_count, path, created_at)
  VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)`);

export function insertProcessSamples(ts: number, processes: ProcessSample[]): void {
  db.transaction(() => {
    for (const p of processes) {
      insertProcStmt.run(ts, p.pid, p.name, p.commitMb, p.wsMb, p.cpuPct,
        p.growthMbps, p.peakGrowthMbps, p.suspiciousCount, p.path, p.createdAt);
    }
  })();
}

function rowToSnapshot(r: Row): Snapshot {
  return {
    ts: num(r.ts),
    memory: {
      totalPhysMb: num(r.total_phys_mb), availPhysMb: num(r.avail_phys_mb), physPct: num(r.phys_pct),
      totalPfMb: num(r.total_pf_mb), availPfMb: num(r.avail_pf_mb), pfPct: num(r.pf_pct),
      totalVirtualMb: 0, freeVirtualMb: 0,
    },
    cpuTotalPct: num(r.cpu_total_pct),
    gpu: {
      available: r.gpu_pct !== null,
      utilizationPct: r.gpu_pct === null ? null : num(r.gpu_pct),
      vramUsedMb: r.gpu_vram_used_mb === null ? null : num(r.gpu_vram_used_mb),
      vramTotalMb: r.gpu_vram_total_mb === null ? null : num(r.gpu_vram_total_mb),
      name: null, source: 'history',
    },
    idleSec: num(r.idle_sec), processCount: num(r.proc_count),
    uptimeSec: 0, demo: bool(r.demo),
  };
}

export function getLatestSnapshot(): Snapshot | null {
  const row = db.prepare('SELECT * FROM snapshots ORDER BY ts DESC LIMIT 1').get() as Row | undefined;
  return row ? rowToSnapshot(row) : null;
}

function rowToProcess(r: Row): ProcessSample {
  return {
    pid: num(r.pid), name: str(r.name), commitMb: num(r.commit_mb), wsMb: num(r.ws_mb),
    cpuPct: num(r.cpu_pct), growthMbps: num(r.growth_mbps),
    peakGrowthMbps: num(r.peak_growth_mbps), suspiciousCount: num(r.suspicious_count),
    path: r.path === null ? null : str(r.path),
    createdAt: r.created_at === null ? null : num(r.created_at),
    whitelisted: false,
  };
}

export function getLatestProcesses(): ProcessSample[] {
  const rows = db.prepare(`SELECT * FROM proc_samples
    WHERE ts = (SELECT MAX(ts) FROM proc_samples) ORDER BY commit_mb DESC`).all() as Row[];
  return rows.map(rowToProcess);
}

export function queryHistory(from: number, to: number, maxPoints = 500): HistoryPoint[] {
  const step = Math.max(1, Math.ceil((to - from) / maxPoints));
  const rows = db.prepare(`SELECT
      CAST(ts / ? AS INTEGER) AS bucket,
      MIN(ts) AS ts,
      AVG(pf_pct) AS pf, AVG(phys_pct) AS phys,
      AVG(cpu_total_pct) AS cpu, AVG(gpu_pct) AS gpu,
      AVG(proc_count) AS procs
    FROM snapshots WHERE ts BETWEEN ? AND ?
    GROUP BY bucket ORDER BY bucket`).all(step, from, to) as Row[];
  return rows.map((r) => ({
    ts: num(r.ts), pfPct: num(r.pf), physPct: num(r.phys), cpuTotalPct: num(r.cpu),
    gpuPct: r.gpu === null ? null : num(r.gpu), processCount: num(r.procs),
  }));
}

export function queryAggregated(from: number, to: number, bucketMs: number): AggregatedPoint[] {
  const rows = db.prepare(`SELECT
      CAST(ts / ? AS INTEGER) AS bucket,
      MIN(ts) AS ts,
      AVG(pf_pct) AS avg_pf, MAX(pf_pct) AS max_pf,
      AVG(phys_pct) AS avg_phys, MAX(phys_pct) AS max_phys,
      AVG(cpu_total_pct) AS avg_cpu, MAX(cpu_total_pct) AS max_cpu,
      COUNT(*) AS samples
    FROM snapshots WHERE ts BETWEEN ? AND ?
    GROUP BY bucket ORDER BY bucket`).all(bucketMs, from, to) as Row[];
  return rows.map((r) => ({
    bucket: new Date(num(r.ts)).toISOString(), ts: num(r.ts),
    avgPf: num(r.avg_pf), maxPf: num(r.max_pf),
    avgPhys: num(r.avg_phys), maxPhys: num(r.max_phys),
    avgCpu: num(r.avg_cpu), maxCpu: num(r.max_cpu), samples: num(r.samples),
  }));
}

export function getSnapshotStats(): { total: number; oldest: number | null; newest: number | null } {
  const r = db.prepare('SELECT COUNT(*) AS c, MIN(ts) AS lo, MAX(ts) AS hi FROM snapshots').get() as Row;
  return { total: num(r.c), oldest: r.lo === null ? null : num(r.lo), newest: r.hi === null ? null : num(r.hi) };
}

export function pruneData(retentionDays: number): number {
  const cutoff = Date.now() - retentionDays * 86_400_000;
  // Per-process rows grow fastest, so cap them at 7 days regardless of retention.
  const procCutoff = Date.now() - Math.min(retentionDays, 7) * 86_400_000;
  const r1 = db.prepare('DELETE FROM snapshots WHERE ts < ?').run(cutoff).changes;
  const r2 = db.prepare('DELETE FROM proc_samples WHERE ts < ?').run(procCutoff).changes;
  const r3 = db.prepare('DELETE FROM alerts WHERE ts < ?').run(cutoff).changes;
  return r1 + r2 + r3;
}

export function clearHistory(): number {
  const a = db.prepare('DELETE FROM snapshots').run().changes;
  const b = db.prepare('DELETE FROM proc_samples').run().changes;
  return a + b;
}

export function queryProcessHistory(pid: number, limit = 120): Array<{
  ts: number; commitMb: number; wsMb: number; cpuPct: number; growthMbps: number;
}> {
  const rows = db.prepare(`SELECT ts, commit_mb, ws_mb, cpu_pct, growth_mbps
    FROM proc_samples WHERE pid = ? ORDER BY ts DESC LIMIT ?`)
    .all(pid, Math.min(limit, 500)) as Row[];
  return rows.map((r) => ({
    ts: num(r.ts), commitMb: num(r.commit_mb), wsMb: num(r.ws_mb),
    cpuPct: num(r.cpu_pct), growthMbps: num(r.growth_mbps),
  })).reverse();
}

/* -------------------------------- alerts --------------------------------- */

function rowToAlert(r: Row): AlertEvent {
  return {
    id: num(r.id), ts: num(r.ts), ruleId: str(r.rule_id), type: str(r.type) as AlertEvent['type'],
    severity: str(r.severity) as Severity, pid: r.pid === null ? null : num(r.pid),
    procName: r.proc_name === null ? null : str(r.proc_name),
    value: num(r.value), threshold: num(r.threshold), message: str(r.message),
    acknowledged: bool(r.acknowledged),
  };
}

export interface AlertFilter { since?: number; severity?: Severity; acknowledged?: boolean; limit?: number }

export function listAlerts(filter: AlertFilter = {}): AlertEvent[] {
  const where: string[] = [];
  const args: unknown[] = [];
  if (filter.since) { where.push('ts >= ?'); args.push(filter.since); }
  if (filter.severity) { where.push('severity = ?'); args.push(filter.severity); }
  if (filter.acknowledged !== undefined) { where.push('acknowledged = ?'); args.push(filter.acknowledged ? 1 : 0); }
  const sql = `SELECT * FROM alerts ${where.length ? 'WHERE ' + where.join(' AND ') : ''}
    ORDER BY ts DESC LIMIT ?`;
  args.push(Math.min(filter.limit ?? 200, 1000));
  return (db.prepare(sql).all(...args) as Row[]).map(rowToAlert);
}

export function insertAlert(a: Omit<AlertEvent, 'id' | 'acknowledged'>): AlertEvent {
  const info = db.prepare(`INSERT INTO alerts
    (ts, rule_id, type, severity, pid, proc_name, value, threshold, message)
    VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)`).run(
    a.ts, a.ruleId, a.type, a.severity, a.pid, a.procName, a.value, a.threshold, a.message,
  );
  const row = db.prepare('SELECT * FROM alerts WHERE id = ?').get(info.lastInsertRowid) as Row;
  return rowToAlert(row);
}

export function ackAlert(id: number): boolean {
  return db.prepare('UPDATE alerts SET acknowledged = 1 WHERE id = ?').run(id).changes > 0;
}

export function ackAllAlerts(): number {
  return db.prepare('UPDATE alerts SET acknowledged = 1 WHERE acknowledged = 0').run().changes;
}

export function deleteAlerts(before?: number): number {
  return before
    ? db.prepare('DELETE FROM alerts WHERE ts < ?').run(before).changes
    : db.prepare('DELETE FROM alerts').run().changes;
}

/* --------------------------------- rules ---------------------------------- */

function rowToRule(r: Row): AlertRule {
  return {
    id: str(r.id), name: str(r.name), metric: str(r.metric) as AlertRule['metric'],
    op: str(r.op) as 'gt' | 'lt', threshold: num(r.threshold),
    consecutive: num(r.consecutive), severity: str(r.severity) as Severity,
    enabled: bool(r.enabled), cooldownSec: num(r.cooldown_sec), createdAt: num(r.created_at),
  };
}

export function listRules(): AlertRule[] {
  return (db.prepare('SELECT * FROM rules ORDER BY created_at').all() as Row[]).map(rowToRule);
}

export function getRule(id: string): AlertRule | null {
  const r = db.prepare('SELECT * FROM rules WHERE id = ?').get(id) as Row | undefined;
  return r ? rowToRule(r) : null;
}

export function upsertRule(rule: AlertRule): AlertRule {
  db.prepare(`INSERT INTO rules (id, name, metric, op, threshold, consecutive, severity, enabled, cooldown_sec, created_at)
    VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
    ON CONFLICT(id) DO UPDATE SET name=excluded.name, metric=excluded.metric, op=excluded.op,
      threshold=excluded.threshold, consecutive=excluded.consecutive, severity=excluded.severity,
      enabled=excluded.enabled, cooldown_sec=excluded.cooldown_sec`).run(
    rule.id, rule.name, rule.metric, rule.op, rule.threshold, rule.consecutive,
    rule.severity, rule.enabled ? 1 : 0, rule.cooldownSec, rule.createdAt,
  );
  return getRule(rule.id)!;
}

export function deleteRule(id: string): boolean {
  return db.prepare('DELETE FROM rules WHERE id = ?').run(id).changes > 0;
}

export function seedDefaultRules(): void {
  const count = (db.prepare('SELECT COUNT(*) AS c FROM rules').get() as Row).c;
  if (num(count) > 0) return;
  const now = Date.now();
  const defaults: AlertRule[] = [
    { id: 'rule-cpu-hog', name: '进程 CPU 占用过高', metric: 'cpu', op: 'gt', threshold: 50, consecutive: 3, severity: 'warning', enabled: true, cooldownSec: 300, createdAt: now },
    { id: 'rule-mem-hog', name: '进程提交内存过大', metric: 'commit', op: 'gt', threshold: 4096, consecutive: 1, severity: 'warning', enabled: true, cooldownSec: 600, createdAt: now + 1 },
    { id: 'rule-pagefile', name: '页面文件使用率过高', metric: 'pagefile', op: 'gt', threshold: 85, consecutive: 2, severity: 'critical', enabled: true, cooldownSec: 600, createdAt: now + 2 },
    { id: 'rule-phys', name: '物理内存负载过高', metric: 'phys', op: 'gt', threshold: 90, consecutive: 2, severity: 'warning', enabled: true, cooldownSec: 600, createdAt: now + 3 },
    { id: 'rule-gpu', name: 'GPU 使用率过高', metric: 'gpu', op: 'gt', threshold: 90, consecutive: 3, severity: 'info', enabled: true, cooldownSec: 300, createdAt: now + 4 },
    { id: 'rule-growth', name: '进程内存快速增长', metric: 'growth', op: 'gt', threshold: 10, consecutive: 3, severity: 'warning', enabled: true, cooldownSec: 300, createdAt: now + 5 },
  ];
  db.transaction(() => { for (const rule of defaults) upsertRule(rule); })();
}

/* ------------------------------- whitelist -------------------------------- */

export function listWhitelist(): WhitelistEntry[] {
  return (db.prepare('SELECT * FROM whitelist ORDER BY created_at DESC').all() as Row[]).map((r) => ({
    id: num(r.id), pattern: str(r.pattern), note: str(r.note), createdAt: num(r.created_at),
  }));
}

export function addWhitelist(pattern: string, note = ''): WhitelistEntry {
  const now = Date.now();
  const info = db.prepare('INSERT OR IGNORE INTO whitelist (pattern, note, created_at) VALUES (?, ?, ?)')
    .run(pattern, note, now);
  const id = Number(info.lastInsertRowid);
  const row = db.prepare('SELECT * FROM whitelist WHERE id = ?').get(id) as Row | undefined
    ?? db.prepare('SELECT * FROM whitelist WHERE pattern = ?').get(pattern) as Row;
  return { id: num(row.id), pattern: str(row.pattern), note: str(row.note), createdAt: num(row.created_at) };
}

export function deleteWhitelist(id: number): boolean {
  return db.prepare('DELETE FROM whitelist WHERE id = ?').run(id).changes > 0;
}

export function matchesWhitelist(name: string, entries: WhitelistEntry[]): boolean {
  const lower = name.toLowerCase();
  return entries.some((entry) => {
    const pattern = entry.pattern.toLowerCase();
    if (!pattern.includes('*')) return lower === pattern;
    const escaped = pattern.replace(/[.+?^${}()|[\]\\]/g, '\\$&').replace(/\*/g, '.*');
    return new RegExp(`^${escaped}$`).test(lower);
  });
}

/* -------------------------------- actions --------------------------------- */

export function logAction(kind: string, detail: string, ok: boolean): ActionLog {
  const ts = Date.now();
  const info = db.prepare('INSERT INTO actions (ts, kind, detail, ok) VALUES (?, ?, ?, ?)')
    .run(ts, kind, detail, ok ? 1 : 0);
  return { id: Number(info.lastInsertRowid), ts, kind, detail, ok };
}

export function listActions(limit = 100): ActionLog[] {
  return (db.prepare('SELECT * FROM actions ORDER BY ts DESC LIMIT ?').all(Math.min(limit, 500)) as Row[])
    .map((r) => ({ id: num(r.id), ts: num(r.ts), kind: str(r.kind), detail: str(r.detail), ok: bool(r.ok) }));
}

/* --------------------------------- export --------------------------------- */

export type ExportKind = 'snapshots' | 'alerts' | 'processes' | 'actions';

const EXPORT_TABLES: Record<ExportKind, { table: string; timeCol: string }> = {
  snapshots: { table: 'snapshots', timeCol: 'ts' },
  alerts: { table: 'alerts', timeCol: 'ts' },
  processes: { table: 'proc_samples', timeCol: 'ts' },
  actions: { table: 'actions', timeCol: 'ts' },
};

/** Whitelisted table names only — no user input is ever interpolated into SQL identifiers. */
export function exportRows(kind: ExportKind, from: number, to: number, limit: number): Row[] {
  const spec = EXPORT_TABLES[kind];
  if (!spec) throw new Error('unsupported export kind');
  return db.prepare(`SELECT * FROM ${spec.table} WHERE ${spec.timeCol} BETWEEN ? AND ? ORDER BY ${spec.timeCol} ASC LIMIT ?`)
    .all(from, to, Math.min(limit, 50_000)) as Row[];
}

export function closeDb(): void {
  db.close();
}
