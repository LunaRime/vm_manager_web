import type { FastifyInstance } from 'fastify';
import type { AppContext } from '../context.js';
import { getSnapshotStats, listActions, listRules, listWhitelist } from '../db.js';
import { broadcast, clientCount, registerClient } from '../events.js';
import type { Snapshot } from '../types.js';

function emptySnapshot(ts: number): Snapshot {
  return {
    ts,
    memory: {
      totalPhysMb: 0, availPhysMb: 0, physPct: 0, totalPfMb: 0, availPfMb: 0,
      pfPct: 0, totalVirtualMb: 0, freeVirtualMb: 0,
    },
    cpuTotalPct: 0,
    gpu: { available: false, utilizationPct: null, vramUsedMb: null, vramTotalMb: null, name: null, source: 'unavailable' },
    idleSec: 0, processCount: 0, uptimeSec: 0, demo: false,
  };
}

export function registerStatusRoutes(app: FastifyInstance, ctx: AppContext): void {
  app.get('/api/health', async () => ({
    ok: true,
    version: '6.0.0',
    demo: ctx.getSnapshot()?.demo ?? false,
    ready: ctx.getSnapshot() !== null,
    collectorError: ctx.getCollectorError(),
    uptimeSec: ctx.serverUptimeSec(),
    sseClients: clientCount(),
  }));

  app.get('/api/events', (request, reply) => {
    reply.hijack();
    registerClient(request.raw, reply.raw);
    broadcast('hello', { ts: Date.now(), demo: ctx.getSnapshot()?.demo ?? false });
  });

  app.get('/api/status', async () => {
    const snapshot = ctx.getSnapshot() ?? emptySnapshot(Date.now());
    return {
      ...snapshot,
      processes: ctx.getProcesses(),
      collectorError: ctx.getCollectorError(),
      serverUptimeSec: ctx.serverUptimeSec(),
    };
  });

  app.get('/api/cpu', async () => ({
    processes: [...ctx.getProcesses()]
      .sort((a, b) => b.cpuPct - a.cpuPct)
      .slice(0, 20),
    ts: ctx.getSnapshot()?.ts ?? Date.now(),
  }));

  app.get('/api/gpu', async () => ({
    ...(ctx.getSnapshot()?.gpu ?? emptySnapshot(Date.now()).gpu),
    ts: ctx.getSnapshot()?.ts ?? Date.now(),
  }));

  app.get('/api/suspicious', async () => ({
    processes: ctx.getSuspicious(),
    ts: ctx.getSnapshot()?.ts ?? Date.now(),
  }));

  app.get('/api/actions', async () => ({ actions: listActions(200) }));

  app.get('/api/stats', async () => ({
    snapshots: getSnapshotStats(),
    rules: listRules().length,
    whitelist: listWhitelist().length,
    sseClients: clientCount(),
  }));
}
