import type { FastifyInstance } from 'fastify';
import { exportRows, queryAggregated, queryHistory, type ExportKind } from '../db.js';
import { httpError, parseTimeRange, queryOf, safeFilename, toCsv } from './util.js';

const AGG_PRESETS: Record<string, { ms: number; bucket: number }> = {
  day: { ms: 86_400_000, bucket: 3_600_000 },
  week: { ms: 7 * 86_400_000, bucket: 6 * 3_600_000 },
  month: { ms: 30 * 86_400_000, bucket: 86_400_000 },
  year: { ms: 365 * 86_400_000, bucket: 30 * 86_400_000 },
};

const EXPORT_KINDS: ExportKind[] = ['snapshots', 'alerts', 'processes', 'actions'];

export function registerHistoryRoutes(app: FastifyInstance): void {
  app.get('/api/history', async (request) => {
    const q = queryOf(request);
    const { from, to, maxPoints, label } = parseTimeRange(q);
    return { range: label, from, to, points: queryHistory(from, to, maxPoints) };
  });

  app.get('/api/aggregated', async (request) => {
    const q = queryOf(request);
    const label = (q.range && AGG_PRESETS[q.range] ? q.range : 'week') as keyof typeof AGG_PRESETS;
    const preset = AGG_PRESETS[label];
    const to = Number(q.to) || Date.now();
    const from = Number(q.from) || to - preset.ms;
    return {
      range: label, from, to, bucketMs: preset.bucket,
      points: queryAggregated(from, to, preset.bucket),
    };
  });

  app.get('/api/export', async (request, reply) => {
    const q = queryOf(request);
    const kind = (q.kind ?? 'snapshots') as ExportKind;
    const format = q.format === 'csv' ? 'csv' : 'json';
    if (!EXPORT_KINDS.includes(kind)) return httpError(reply, 400, 'BAD_KIND', '不支持的导出类型');

    const { from, to } = parseTimeRange({ ...q, range: q.range ?? '30d' });
    const rows = exportRows(kind, from, to, 50_000);
    const filename = safeFilename(`vm-${kind}-${new Date(to).toISOString().slice(0, 10)}.${format}`);
    reply.header('Content-Disposition', `attachment; filename="${filename}"`);
    if (format === 'csv') {
      return reply.type('text/csv; charset=utf-8').send(toCsv(rows));
    }
    return reply.type('application/json; charset=utf-8').send(JSON.stringify({
      kind, from, to, count: rows.length, rows,
    }, null, 2));
  });
}
