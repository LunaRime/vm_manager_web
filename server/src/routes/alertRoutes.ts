import { randomUUID } from 'node:crypto';
import type { FastifyInstance } from 'fastify';
import { ruleInputSchema, resetRuleState } from '../alerts.js';
import {
  ackAlert, ackAllAlerts, deleteAlerts, deleteRule, listAlerts,
  listRules, upsertRule,
} from '../db.js';
import { broadcast } from '../events.js';
import type { Severity } from '../types.js';
import { httpError, paramsOf, queryOf } from './util.js';

const SEVERITIES: Severity[] = ['info', 'warning', 'critical'];

export function registerAlertRoutes(app: FastifyInstance): void {
  app.get('/api/alerts', async (request) => {
    const q = queryOf(request);
    const severity = SEVERITIES.includes(q.severity as Severity) ? q.severity as Severity : undefined;
    const acknowledged = q.acknowledged === 'true' ? true : q.acknowledged === 'false' ? false : undefined;
    return {
      alerts: listAlerts({
        since: Number(q.since) || undefined,
        severity,
        acknowledged,
        limit: Number(q.limit) || 200,
      }),
    };
  });

  app.post('/api/alerts/:id/ack', async (request, reply) => {
    const id = Number(paramsOf(request).id);
    if (!Number.isInteger(id) || id < 1) return httpError(reply, 400, 'BAD_ID', '无效的告警 ID');
    if (!ackAlert(id)) return httpError(reply, 404, 'NOT_FOUND', '告警不存在');
    broadcast('action', { kind: 'ack', id, ts: Date.now() });
    return { ok: true, id };
  });

  app.post('/api/alerts/ack-all', async () => {
    const count = ackAllAlerts();
    return { ok: true, count };
  });

  app.delete('/api/alerts', async (request, reply) => {
    const q = queryOf(request);
    if (q.confirm !== 'true') return httpError(reply, 400, 'CONFIRM_REQUIRED', '清空告警需要 confirm=true');
    const before = Number(q.before) || undefined;
    const count = deleteAlerts(before);
    return { ok: true, count };
  });

  app.get('/api/rules', async () => ({ rules: listRules() }));

  app.post('/api/rules', async (request, reply) => {
    const parsed = ruleInputSchema.safeParse(request.body);
    if (!parsed.success) return httpError(reply, 400, 'BAD_INPUT', parsed.error.issues[0]?.message ?? '参数错误');
    const input = parsed.data;
    const id = input.id ?? `rule-${Date.now().toString(36)}-${randomUUID().slice(0, 6)}`;
    const rule = upsertRule({
      id,
      name: input.name, metric: input.metric, op: input.op,
      threshold: input.threshold, consecutive: input.consecutive,
      severity: input.severity, enabled: input.enabled,
      cooldownSec: input.cooldownSec,
      createdAt: Date.now(),
    });
    resetRuleState();
    broadcast('settings', { kind: 'rules', rule });
    return reply.code(201).send({ rule });
  });

  app.delete('/api/rules/:id', async (request, reply) => {
    const id = paramsOf(request).id;
    if (!id) return httpError(reply, 400, 'BAD_ID', '缺少规则 ID');
    if (!deleteRule(id)) return httpError(reply, 404, 'NOT_FOUND', '规则不存在');
    resetRuleState();
    return { ok: true, id };
  });
}
