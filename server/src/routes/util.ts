import type { FastifyReply } from 'fastify';

export function httpError(reply: FastifyReply, status: number, code: string, message: string) {
  return reply.code(status).send({ error: { code, message } });
}

export function queryOf(request: { query: unknown }): Record<string, string | undefined> {
  return (request.query ?? {}) as Record<string, string | undefined>;
}

export function paramsOf(request: { params: unknown }): Record<string, string | undefined> {
  return (request.params ?? {}) as Record<string, string | undefined>;
}

export function parsePid(value: unknown): number | null {
  const pid = Number(value);
  if (!Number.isInteger(pid) || pid < 10 || pid > 0x7fffffff) return null;
  return pid;
}

export function parseTimeRange(
  q: Record<string, string | undefined>,
): { from: number; to: number; maxPoints: number; label: string } {
  const to = Number(q.to) || Date.now();
  const explicitFrom = Number(q.from);
  if (Number.isFinite(explicitFrom) && explicitFrom > 0 && q.from) {
    return { from: explicitFrom, to, maxPoints: 500, label: 'custom' };
  }
  const presets: Record<string, { ms: number; points: number }> = {
    '1h': { ms: 3_600_000, points: 120 },
    '24h': { ms: 86_400_000, points: 288 },
    '7d': { ms: 7 * 86_400_000, points: 400 },
    '30d': { ms: 30 * 86_400_000, points: 500 },
    '1y': { ms: 365 * 86_400_000, points: 500 },
  };
  const label = q.range && presets[q.range] ? q.range : '24h';
  const preset = presets[label];
  return { from: to - preset.ms, to, maxPoints: preset.points, label };
}

/** RFC 4180-ish CSV with proper quoting for values coming from user/system data. */
export function toCsv(rows: Array<Record<string, unknown>>): string {
  if (rows.length === 0) return '';
  const headers = Object.keys(rows[0]);
  const escape = (v: unknown): string => {
    if (v === null || v === undefined) return '';
    const s = String(v).replace(/\r?\n/g, ' ');
    return /[",;]/.test(s) ? `"${s.replace(/"/g, '""')}"` : s;
  };
  const lines = [headers.join(',')];
  for (const row of rows) lines.push(headers.map((h) => escape(row[h])).join(','));
  return '\uFEFF' + lines.join('\r\n');
}

export function safeFilename(name: string): string {
  return name.replace(/[^A-Za-z0-9._-]/g, '_');
}
