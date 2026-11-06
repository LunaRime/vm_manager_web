import { execFile } from 'node:child_process';
import { promisify } from 'node:util';
import type { FastifyInstance, FastifyReply } from 'fastify';
import { z } from 'zod';
import type { AppContext } from '../context.js';
import {
  addWhitelist, deleteWhitelist, listWhitelist, logAction,
  matchesWhitelist, queryProcessHistory,
} from '../db.js';
import { broadcast } from '../events.js';
import { httpError, paramsOf, parsePid, queryOf } from './util.js';

const execFileAsync = promisify(execFile);

const PROTECTED_NAMES = new Set([
  'system', 'smss.exe', 'csrss.exe', 'wininit.exe', 'services.exe',
  'lsass.exe', 'winlogon.exe', 'fontdrvhost.exe',
]);

const whitelistSchema = z.object({
  pattern: z.string().min(1).max(128).regex(/^[A-Za-z0-9_.\-*]+$/, '仅允许字母、数字、点、下划线、连字符与通配符 *'),
  note: z.string().max(200).optional().default(''),
});

const PROCESS_ACTION_SCRIPT = String.raw`
$ErrorActionPreference = 'Stop'
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class VMProcCtl {
  [DllImport("ntdll.dll")] public static extern int NtSuspendProcess(IntPtr handle);
  [DllImport("ntdll.dll")] public static extern int NtResumeProcess(IntPtr handle);
  [DllImport("kernel32.dll")] public static extern IntPtr OpenProcess(uint access, bool inherit, int pid);
  [DllImport("kernel32.dll")] public static extern bool CloseHandle(IntPtr handle);
}
'@
$target = [int]$env:VM_TARGET_PID
$handle = [VMProcCtl]::OpenProcess(0x0800, $false, $target)
if ($handle -eq [IntPtr]::Zero) { Write-Error 'OpenProcess failed (access denied or PID gone)'; exit 1 }
try {
  if ($env:VM_ACTION -eq 'suspend') { $status = [VMProcCtl]::NtSuspendProcess($handle) }
  else { $status = [VMProcCtl]::NtResumeProcess($handle) }
  if ($status -lt 0) { Write-Error ('NtSuspendProcess/NtResumeProcess failed: ' + $status); exit 1 }
} finally {
  [VMProcCtl]::CloseHandle($handle) | Out-Null
}
Write-Output 'ok'
`;

async function guard(
  pidRaw: unknown, ctx: AppContext, reply: FastifyReply, requireConfirm: boolean, request: { body?: unknown },
): Promise<{ pid: number; name: string } | null> {
  const pid = parsePid(pidRaw);
  if (pid === null) { httpError(reply, 400, 'BAD_PID', '无效的进程 PID'); return null; }
  if (pid === process.pid) { httpError(reply, 403, 'SELF_PID', '不能操作监控服务自身'); return null; }
  const proc = ctx.getProcesses().find((p) => p.pid === pid);
  const name = proc?.name ?? 'unknown';
  if (PROTECTED_NAMES.has(name.toLowerCase())) {
    httpError(reply, 403, 'PROTECTED', `系统关键进程 ${name} 禁止操作`);
    return null;
  }
  if (matchesWhitelist(name, listWhitelist())) {
    httpError(reply, 403, 'WHITELISTED', `${name} 在白名单中，禁止操作`);
    return null;
  }
  if (requireConfirm) {
    const body = request.body as { confirm?: unknown } | null;
    if (body?.confirm !== true) {
      httpError(reply, 400, 'CONFIRM_REQUIRED', '危险操作需要显式确认 (confirm: true)');
      return null;
    }
  }
  return { pid, name };
}

function finish(reply: FastifyReply, kind: string, detail: string, ok: boolean, status = 200) {
  const entry = logAction(kind, detail, ok);
  broadcast('action', entry);
  return reply.code(status).send({ ok, action: entry });
}

export function registerProcessRoutes(app: FastifyInstance, ctx: AppContext): void {
  app.get('/api/processes', async (request) => {
    const q = queryOf(request);
    const search = (q.search ?? '').toLowerCase();
    const sort = ['commitMb', 'cpuPct', 'wsMb', 'growthMbps', 'name', 'pid'].includes(q.sort ?? '')
      ? q.sort! : 'commitMb';
    const desc = q.order !== 'asc';
    const limit = Math.min(Math.max(Number(q.limit) || 100, 1), 500);
    let list = ctx.getProcesses().filter((p) =>
      !search || p.name.toLowerCase().includes(search) || String(p.pid).includes(search));
    if (q.whitelistedOnly === 'true') list = list.filter((p) => p.whitelisted);
    list = [...list].sort((a, b) => {
      const av = a[sort as keyof typeof a];
      const bv = b[sort as keyof typeof b];
      const cmp = typeof av === 'string' ? String(av).localeCompare(String(bv)) : Number(av) - Number(bv);
      return desc ? -cmp : cmp;
    });
    return { ts: ctx.getSnapshot()?.ts ?? Date.now(), total: list.length, processes: list.slice(0, limit) };
  });

  app.get('/api/processes/:pid', async (request, reply) => {
    const pid = parsePid(paramsOf(request).pid);
    if (pid === null) return httpError(reply, 400, 'BAD_PID', '无效的进程 PID');
    const current = ctx.getProcesses().find((p) => p.pid === pid) ?? null;
    return { process: current, history: queryProcessHistory(pid, 120) };
  });

  app.post('/api/processes/:pid/terminate', async (request, reply) => {
    const target = await guard(paramsOf(request).pid, ctx, reply, true, request);
    if (!target) return reply;
    try {
      await execFileAsync('taskkill.exe', ['/PID', String(target.pid), '/F'], { timeout: 10_000, windowsHide: true });
      void ctx.requestSample();
      return finish(reply, 'terminate', `结束进程 ${target.name} (PID ${target.pid})`, true);
    } catch (err) {
      const message = err instanceof Error ? err.message : String(err);
      return finish(reply, 'terminate', `结束 ${target.name} 失败: ${message}`, false, 500);
    }
  });

  app.post('/api/processes/:pid/suspend', async (request, reply) => {
    const target = await guard(paramsOf(request).pid, ctx, reply, true, request);
    if (!target) return reply;
    return runProcAction(target, 'suspend', reply, ctx);
  });

  app.post('/api/processes/:pid/resume', async (request, reply) => {
    const target = await guard(paramsOf(request).pid, ctx, reply, false, request);
    if (!target) return reply;
    return runProcAction(target, 'resume', reply, ctx);
  });

  app.get('/api/whitelist', async () => ({ entries: listWhitelist() }));

  app.post('/api/whitelist', async (request, reply) => {
    const parsed = whitelistSchema.safeParse(request.body);
    if (!parsed.success) return httpError(reply, 400, 'BAD_INPUT', parsed.error.issues[0]?.message ?? '参数错误');
    const entry = addWhitelist(parsed.data.pattern, parsed.data.note);
    void ctx.requestSample();
    return finish(reply, 'whitelist', `加入白名单 ${entry.pattern}`, true, 201);
  });

  app.delete('/api/whitelist/:id', async (request, reply) => {
    const id = Number(paramsOf(request).id);
    if (!Number.isInteger(id) || id < 1) return httpError(reply, 400, 'BAD_ID', '无效的白名单 ID');
    const ok = deleteWhitelist(id);
    if (!ok) return httpError(reply, 404, 'NOT_FOUND', '白名单条目不存在');
    return finish(reply, 'whitelist', `移除白名单条目 #${id}`, true);
  });
}

async function runProcAction(
  target: { pid: number; name: string }, action: 'suspend' | 'resume',
  reply: FastifyReply, ctx: AppContext,
) {
  const encoded = Buffer.from(PROCESS_ACTION_SCRIPT, 'utf16le').toString('base64');
  try {
    await execFileAsync('powershell.exe', [
      '-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass', '-EncodedCommand', encoded,
    ], {
      timeout: 15_000, windowsHide: true,
      env: { ...process.env, VM_TARGET_PID: String(target.pid), VM_ACTION: action },
    });
    void ctx.requestSample();
    return finish(reply, action, `${action === 'suspend' ? '挂起' : '恢复'}进程 ${target.name} (PID ${target.pid})`, true);
  } catch (err) {
    const message = err instanceof Error ? err.message : String(err);
    return finish(reply, action, `${action} ${target.name} 失败: ${message}`, false, 500);
  }
}
