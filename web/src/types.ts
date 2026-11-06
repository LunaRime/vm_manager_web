/** Mirrors server/src/types.ts — kept in sync by the API smoke tests. */

export type Severity = 'info' | 'warning' | 'critical';
export type AlertMetric = 'cpu' | 'commit' | 'gpu' | 'pagefile' | 'phys' | 'growth';
export type Language = 'zh-CN' | 'zh-TW' | 'en';
export type Theme = 'light' | 'dark';

export interface MemoryInfo {
  totalPhysMb: number; availPhysMb: number; physPct: number;
  totalPfMb: number; availPfMb: number; pfPct: number;
  totalVirtualMb: number; freeVirtualMb: number;
}

export interface GpuInfo {
  available: boolean; utilizationPct: number | null;
  vramUsedMb: number | null; vramTotalMb: number | null;
  name: string | null; source: string;
}

export interface ProcessSample {
  pid: number; name: string; commitMb: number; wsMb: number; cpuPct: number;
  growthMbps: number; peakGrowthMbps: number; suspiciousCount: number;
  path: string | null; createdAt: number | null; whitelisted: boolean;
}

export interface Snapshot {
  ts: number; memory: MemoryInfo; cpuTotalPct: number; gpu: GpuInfo;
  idleSec: number; processCount: number; uptimeSec: number; demo: boolean;
}

export interface StatusResponse extends Snapshot {
  processes: ProcessSample[];
  collectorError: string | null;
  serverUptimeSec: number;
}

export interface HistoryPoint {
  ts: number; pfPct: number; physPct: number;
  cpuTotalPct: number; gpuPct: number | null; processCount: number;
}

export interface AggregatedPoint {
  bucket: string; ts: number; avgPf: number; maxPf: number;
  avgPhys: number; maxPhys: number; avgCpu: number; maxCpu: number; samples: number;
}

export interface AlertRule {
  id: string; name: string; metric: AlertMetric; op: 'gt' | 'lt';
  threshold: number; consecutive: number; severity: Severity;
  enabled: boolean; cooldownSec: number; createdAt: number;
}

export interface AlertEvent {
  id: number; ts: number; ruleId: string; type: AlertMetric; severity: Severity;
  pid: number | null; procName: string | null; value: number;
  threshold: number; message: string; acknowledged: boolean;
}

export interface WhitelistEntry { id: number; pattern: string; note: string; createdAt: number }
export interface ActionLog { id: number; ts: number; kind: string; detail: string; ok: boolean }

export interface Settings {
  sampleIntervalSec: number; port: number; language: Language; theme: Theme;
  retentionDays: number; notifyWeb: boolean; notifySound: boolean;
  notifyDesktop: boolean; notifyMinSeverity: Severity;
  autoClean: boolean; autoStart: boolean; growthThresholdMbps: number;
}

export interface SettingsMetaItem {
  key: keyof Settings; label: string; min?: number; max?: number;
  hot: boolean; group: string;
}

export interface HealthResponse {
  ok: boolean; version: string; demo: boolean; ready: boolean;
  collectorError: string | null; uptimeSec: number; sseClients: number;
}

export interface ProcessHistoryPoint {
  ts: number; commitMb: number; wsMb: number; cpuPct: number; growthMbps: number;
}

export interface ProcessDetail {
  process: ProcessSample | null;
  history: ProcessHistoryPoint[];
}

export interface StatsResponse {
  snapshots: { total: number; oldest: number | null; newest: number | null };
  rules: number; whitelist: number; sseClients: number;
}

export interface HistoryResponse {
  range: string; from: number; to: number; points: HistoryPoint[];
}

export interface AggregatedResponse {
  range: string; from: number; to: number; bucketMs: number; points: AggregatedPoint[];
}

export type ExportKind = 'snapshots' | 'alerts' | 'processes' | 'actions';
export type ExportFormat = 'csv' | 'json';
