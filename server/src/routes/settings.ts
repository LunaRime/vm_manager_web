import { execFileSync } from 'node:child_process';
import { resolve } from 'node:path';
import type { FastifyInstance } from 'fastify';
import { DEFAULT_SETTINGS, SETTINGS_META, settingsSchema } from '../config.js';
import type { AppContext } from '../context.js';
import { PROJECT_ROOT, clearHistory, loadSettings, pruneData, saveSettings } from '../db.js';
import { broadcast } from '../events.js';
import { httpError, queryOf } from './util.js';

const patchSchema = settingsSchema.partial().strict();
const RUN_KEY = 'HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Run';

/** Only invoked when the user explicitly toggles autoStart in Settings. */
function setAutoStart(enabled: boolean): void {
  if (process.platform !== 'win32') return;
  try {
    if (enabled) {
      const command = `"${resolve(PROJECT_ROOT, 'start.cmd')}"`;
      execFileSync('reg.exe', ['add', RUN_KEY, '/v', 'VMManager', '/t', 'REG_SZ', '/d', command, '/f'],
        { windowsHide: true, stdio: 'ignore' });
    } else {
      execFileSync('reg.exe', ['delete', RUN_KEY, '/v', 'VMManager', '/f'],
        { windowsHide: true, stdio: 'ignore' });
    }
  } catch (err) {
    console.warn('[settings] autostart toggle failed:', err instanceof Error ? err.message : String(err));
  }
}

export function registerSettingsRoutes(app: FastifyInstance, ctx: AppContext): void {
  app.get('/api/settings', async () => ({
    settings: loadSettings(),
    defaults: DEFAULT_SETTINGS,
    meta: SETTINGS_META,
    platform: process.platform,
    demo: ctx.getSnapshot()?.demo ?? false,
  }));

  app.put('/api/settings', async (request, reply) => {
    const parsed = patchSchema.safeParse(request.body);
    if (!parsed.success) return httpError(reply, 400, 'BAD_INPUT', parsed.error.issues[0]?.message ?? '参数错误');
    const current = loadSettings();
    const next = { ...current, ...parsed.data };
    const portChanged = next.port !== current.port;
    const autoStartChanged = next.autoStart !== current.autoStart;

    saveSettings(next);
    if (autoStartChanged) setAutoStart(next.autoStart);
    ctx.applySettings(next);
    broadcast('settings', { settings: next, requiresRestart: portChanged });

    return {
      settings: next,
      requiresRestart: portChanged,
      note: portChanged ? '端口修改将在重启服务后生效' : null,
    };
  });

  app.delete('/api/data', async (request, reply) => {
    const q = queryOf(request);
    if (q.confirm !== 'true') return httpError(reply, 400, 'CONFIRM_REQUIRED', '清空历史数据需要 confirm=true');
    const removed = clearHistory();
    broadcast('action', { kind: 'clear-history', ts: Date.now(), removed });
    return { ok: true, removed };
  });

  app.post('/api/settings/prune', async () => {
    const removed = pruneData(ctx.getSettings().retentionDays);
    return { ok: true, removed };
  });

  app.post('/api/collect', async () => {
    await ctx.requestSample();
    return { ok: true, ts: ctx.getSnapshot()?.ts ?? null, error: ctx.getCollectorError() };
  });
}
