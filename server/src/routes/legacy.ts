import type { FastifyInstance, FastifyReply } from 'fastify';
import { listAlerts } from '../db.js';

/** Read-only compatibility shims for the v5 dashboard clients. */
export function registerLegacyRoutes(app: FastifyInstance): void {
  app.get('/api/anomalies', async () => ({ alerts: listAlerts({ limit: 100 }) }));

  const gone = (reply: FastifyReply, message: string) => reply.code(410).send({
    ok: false,
    msg: message,
  });

  app.get('/api/stopserver', async (_request, reply) =>
    gone(reply, 'v6 已移除远程停止接口，请在服务端按 Ctrl+C 或结束进程'));
  app.get('/api/shutdown', async (_request, reply) =>
    gone(reply, 'v6 已移除远程关机接口，请在服务端按 Ctrl+C 或结束进程'));
}
