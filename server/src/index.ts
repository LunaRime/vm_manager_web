import fastifyStatic from '@fastify/static';
import { existsSync, readFileSync } from 'node:fs';
import { resolve } from 'node:path';
import Fastify from 'fastify';
import { evaluateRules, resetRuleState, severityRank } from './alerts.js';
import { collectOnce, collectorUptimeSec } from './collector.js';
import type { AppContext } from './context.js';
import {
  PROJECT_ROOT, WEB_DIST, closeDb, getLatestProcesses, getLatestSnapshot,
  insertProcessSamples, insertSnapshot, listWhitelist, loadSettings,
  matchesWhitelist, pruneData, seedDefaultRules,
} from './db.js';
import { broadcast } from './events.js';
import { sendDesktopNotification } from './notify.js';
import { registerAlertRoutes } from './routes/alertRoutes.js';
import { registerHistoryRoutes } from './routes/history.js';
import { registerLegacyRoutes } from './routes/legacy.js';
import { registerProcessRoutes } from './routes/processes.js';
import { registerSettingsRoutes } from './routes/settings.js';
import { registerStatusRoutes } from './routes/status.js';
import type { ProcessSample, Settings, Snapshot, SuspiciousProcess } from './types.js';

const DEMO = process.env.VM_DEMO === '1' || process.env.VM_DEMO === 'true';
const HOST = process.env.VM_HOST || '127.0.0.1';

seedDefaultRules();
let settings: Settings = loadSettings();
let cachedWhitelist = listWhitelist();
let latestSnapshot: Snapshot | null = getLatestSnapshot();
let latestProcesses: ProcessSample[] = getLatestProcesses();
let suspiciousProcesses: SuspiciousProcess[] = [];
let collectorError: string | null = null;
let currentRun: Promise<void> | null = null;
let timer: NodeJS.Timeout | null = null;
let lastPrune = 0;

async function doSample(): Promise<void> {
  try {
    cachedWhitelist = listWhitelist();
    const result = await collectOnce({
      demo: DEMO,
      growthThresholdMbps: settings.growthThresholdMbps,
      whitelistMatch: (name) => matchesWhitelist(name, cachedWhitelist),
    });

    latestSnapshot = result.snapshot;
    latestProcesses = result.processes;
    suspiciousProcesses = result.suspicious;
    collectorError = null;

    insertSnapshot(result.snapshot);
    // Persist only the top consumers + suspicious ones to keep the DB small.
    const suspiciousPids = new Set(result.suspicious.map((p) => p.pid));
    const persisted = result.processes.filter((p, i) => i < 40 || suspiciousPids.has(p.pid));
    insertProcessSamples(result.snapshot.ts, persisted);

    const alerts = evaluateRules(result.snapshot, result.processes);
    broadcast('snapshot', { snapshot: result.snapshot, processes: result.processes.slice(0, 20) });
    for (const alert of alerts) {
      broadcast('alert', alert);
      if (settings.notifyDesktop && severityRank(alert.severity) >= severityRank(settings.notifyMinSeverity)) {
        void sendDesktopNotification(alert);
      }
    }

    if (settings.autoClean && Date.now() - lastPrune > 86_400_000) {
      lastPrune = Date.now();
      pruneData(settings.retentionDays);
    }
  } catch (err) {
    collectorError = err instanceof Error ? err.message : String(err);
    console.error('[collector]', collectorError);
    broadcast('error', { message: collectorError, ts: Date.now() });
  }
}

/** Coalesces concurrent requests: a manual collect waits for an in-flight sample instead of silently skipping. */
function runSample(): Promise<void> {
  if (currentRun) return currentRun;
  currentRun = doSample().finally(() => { currentRun = null; });
  return currentRun;
}

function schedule(): void {
  if (timer) clearTimeout(timer);
  const delayMs = Math.max(5, settings.sampleIntervalSec) * 1000;
  timer = setTimeout(() => { void runSample().finally(schedule); }, delayMs);
}

const ctx: AppContext = {
  getSnapshot: () => latestSnapshot,
  getProcesses: () => latestProcesses.map((p) => ({ ...p, whitelisted: matchesWhitelist(p.name, cachedWhitelist) })),
  getSuspicious: () => suspiciousProcesses,
  getSettings: () => settings,
  applySettings: (next) => {
    settings = next;
    resetRuleState();
    schedule();
  },
  getCollectorError: () => collectorError,
  requestSample: () => runSample(),
  serverUptimeSec: () => collectorUptimeSec(),
};

const app = Fastify({ logger: { level: 'warn' }, bodyLimit: 1_048_576 });

app.setErrorHandler((err, _request, reply) => {
  const error = err instanceof Error ? err : new Error(String(err));
  const rawStatus = (err as { statusCode?: unknown })?.statusCode;
  const status = typeof rawStatus === 'number' ? rawStatus : 500;
  console.error('[http]', error.message);
  return reply.code(status).send({
    error: {
      code: status >= 500 ? 'INTERNAL_ERROR' : 'BAD_REQUEST',
      message: status >= 500 ? '服务内部错误' : error.message,
    },
  });
});

registerStatusRoutes(app, ctx);
registerHistoryRoutes(app);
registerProcessRoutes(app, ctx);
registerAlertRoutes(app);
registerSettingsRoutes(app, ctx);
registerLegacyRoutes(app);

const webIndex = resolve(WEB_DIST, 'index.html');
if (existsSync(webIndex)) {
  await app.register(fastifyStatic, { root: WEB_DIST, prefix: '/', index: ['index.html'] });
  app.setNotFoundHandler((request, reply) => {
    if (request.url.startsWith('/api/')) {
      return reply.code(404).send({ error: { code: 'NOT_FOUND', message: 'API 端点不存在' } });
    }
    return reply.type('text/html; charset=utf-8').send(readFileSync(webIndex, 'utf8'));
  });
} else {
  app.get('/', async (_request, reply) => reply.type('text/html; charset=utf-8').send(
    `<!doctype html><meta charset="utf-8"><title>VM Manager</title>
     <body style="font:16px system-ui;padding:40px">
     <h1>VM Manager v6 API 已启动</h1>
     <p>前端产物未构建。开发环境请运行 <code>npm run dev</code> 并访问 http://127.0.0.1:5173；
     生产环境请先运行 <code>npm run build</code> 后重启服务。</p>
     <p>API 健康检查：<a href="/api/health">/api/health</a></p></body>`,
  ));
}

async function listenWithFallback(startPort: number): Promise<number> {
  for (let port = startPort; port < startPort + 6; port += 1) {
    try {
      await app.listen({ port, host: HOST });
      return port;
    } catch (err) {
      const code = (err as NodeJS.ErrnoException).code;
      if (code === 'EADDRINUSE' && port < startPort + 5) {
        console.warn(`[server] 端口 ${port} 被占用，尝试 ${port + 1}`);
        continue;
      }
      throw err;
    }
  }
  throw new Error('no available port');
}

const port = Number(process.env.VM_PORT) || settings.port;
try {
  const actualPort = await listenWithFallback(port);
  console.log(`[VM Manager v6] http://${HOST}:${actualPort} | demo=${DEMO} | db=${resolve(PROJECT_ROOT, 'vm_manager.sqlite')}`);
  void runSample().then(schedule);
} catch (err) {
  console.error('[server] 启动失败:', err instanceof Error ? err.message : String(err));
  closeDb();
  process.exit(1);
}

for (const signal of ['SIGINT', 'SIGTERM'] as const) {
  process.on(signal, async () => {
    if (timer) clearTimeout(timer);
    await app.close();
    closeDb();
    process.exit(0);
  });
}
