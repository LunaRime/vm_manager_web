import type {
  ActionLog, AggregatedResponse, AlertEvent, AlertRule, ExportFormat, ExportKind,
  HealthResponse, HistoryResponse, ProcessDetail, ProcessSample, Settings,
  SettingsMetaItem, StatsResponse, StatusResponse, WhitelistEntry,
} from './types';

export class ApiError extends Error {
  constructor(public code: string, message: string, public status: number) {
    super(message);
  }
}

async function request<T>(method: string, url: string, body?: unknown): Promise<T> {
  const response = await fetch(url, {
    method,
    headers: body === undefined ? undefined : { 'Content-Type': 'application/json' },
    body: body === undefined ? undefined : JSON.stringify(body),
  });
  const text = await response.text();
  let data: unknown = null;
  try { data = text ? JSON.parse(text) : null; } catch { /* non-JSON response */ }
  if (!response.ok) {
    const err = (data as { error?: { code?: string; message?: string } } | null)?.error;
    throw new ApiError(err?.code ?? 'HTTP_ERROR', err?.message ?? `HTTP ${response.status}`, response.status);
  }
  return data as T;
}

const get = <T>(url: string) => request<T>('GET', url);

export interface SettingsResponse {
  settings: Settings;
  defaults: Settings;
  meta: SettingsMetaItem[];
  platform: string;
  demo: boolean;
}

export interface SaveSettingsResponse {
  settings: Settings;
  requiresRestart: boolean;
  note: string | null;
}

export const api = {
  health: () => get<HealthResponse>('/api/health'),
  status: () => get<StatusResponse>('/api/status'),
  stats: () => get<StatsResponse>('/api/stats'),
  actions: () => get<{ actions: ActionLog[] }>('/api/actions'),
  history: (range: string) => get<HistoryResponse>(`/api/history?range=${encodeURIComponent(range)}`),
  aggregated: (range: string) => get<AggregatedResponse>(`/api/aggregated?range=${encodeURIComponent(range)}`),
  alerts: () => get<{ alerts: AlertEvent[] }>('/api/alerts?limit=300'),
  ackAlert: (id: number) => request<{ ok: boolean }>('POST', `/api/alerts/${id}/ack`),
  ackAllAlerts: () => request<{ ok: boolean; count: number }>('POST', '/api/alerts/ack-all'),
  clearAlerts: () => request<{ ok: boolean; count: number }>('DELETE', '/api/alerts?confirm=true'),
  rules: () => get<{ rules: AlertRule[] }>('/api/rules'),
  saveRule: (rule: Omit<AlertRule, 'createdAt' | 'id'> & { id?: string }) =>
    request<{ rule: AlertRule }>('POST', '/api/rules', rule),
  deleteRule: (id: string) => request<{ ok: boolean }>('DELETE', `/api/rules/${encodeURIComponent(id)}`),
  whitelist: () => get<{ entries: WhitelistEntry[] }>('/api/whitelist'),
  addWhitelist: (pattern: string, note: string) =>
    request<{ ok: boolean }>('POST', '/api/whitelist', { pattern, note }),
  deleteWhitelist: (id: number) => request<{ ok: boolean }>('DELETE', `/api/whitelist/${id}`),
  processes: (search: string, sort: string, limit = 200) =>
    get<{ processes: ProcessSample[]; total: number }>(
      `/api/processes?search=${encodeURIComponent(search)}&sort=${sort}&limit=${limit}`),
  processDetail: (pid: number) => get<ProcessDetail>(`/api/processes/${pid}`),
  terminate: (pid: number) =>
    request<{ ok: boolean; action: ActionLog }>('POST', `/api/processes/${pid}/terminate`, { confirm: true }),
  suspend: (pid: number) =>
    request<{ ok: boolean; action: ActionLog }>('POST', `/api/processes/${pid}/suspend`, { confirm: true }),
  resume: (pid: number) =>
    request<{ ok: boolean; action: ActionLog }>('POST', `/api/processes/${pid}/resume`, {}),
  settings: () => get<SettingsResponse>('/api/settings'),
  saveSettings: (patch: Partial<Settings>) => request<SaveSettingsResponse>('PUT', '/api/settings', patch),
  collect: () => request<{ ok: boolean; error: string | null }>('POST', '/api/collect'),
  prune: () => request<{ ok: boolean; removed: number }>('POST', '/api/settings/prune'),
  clearData: () => request<{ ok: boolean; removed: number }>('DELETE', '/api/data?confirm=true'),
  exportUrl: (kind: ExportKind, format: ExportFormat, range: string) =>
    `/api/export?kind=${kind}&format=${format}&range=${encodeURIComponent(range)}`,
};

export async function downloadExport(kind: ExportKind, format: ExportFormat, range: string): Promise<void> {
  const url = api.exportUrl(kind, format, range);
  const response = await fetch(url);
  if (!response.ok) throw new ApiError('EXPORT_FAILED', `HTTP ${response.status}`, response.status);
  const blob = await response.blob();
  const disposition = response.headers.get('Content-Disposition') ?? '';
  const match = disposition.match(/filename="([^"]+)"/);
  const filename = match?.[1] ?? `vm-${kind}.${format}`;
  const link = document.createElement('a');
  link.href = URL.createObjectURL(blob);
  link.download = filename;
  document.body.appendChild(link);
  link.click();
  link.remove();
  setTimeout(() => URL.revokeObjectURL(link.href), 2000);
}
