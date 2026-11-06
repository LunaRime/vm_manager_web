import { z } from 'zod';
import { insertAlert, listRules, listWhitelist, matchesWhitelist } from './db.js';
import type { AlertEvent, AlertRule, ProcessSample, Snapshot } from './types.js';

export const METRIC_META: Record<AlertRule['metric'], { label: string; unit: string }> = {
  cpu: { label: 'CPU 占用', unit: '%' },
  commit: { label: '提交内存', unit: 'MB' },
  gpu: { label: 'GPU 使用率', unit: '%' },
  pagefile: { label: '页面文件使用率', unit: '%' },
  phys: { label: '物理内存负载', unit: '%' },
  growth: { label: '内存增长速率', unit: 'MB/s' },
};

export const ruleInputSchema = z.object({
  id: z.string().min(1).max(64).regex(/^[A-Za-z0-9_-]+$/).optional(),
  name: z.string().min(1).max(80),
  metric: z.enum(['cpu', 'commit', 'gpu', 'pagefile', 'phys', 'growth']),
  op: z.enum(['gt', 'lt']),
  threshold: z.number().min(0).max(1_000_000),
  consecutive: z.number().int().min(1).max(60),
  severity: z.enum(['info', 'warning', 'critical']),
  enabled: z.boolean(),
  cooldownSec: z.number().int().min(0).max(86_400),
}).strict();

interface RuleState { count: number; lastFired: number }
const state = new Map<string, RuleState>();

interface Target { key: string; pid: number | null; procName: string | null; value: number }

function targetsFor(rule: AlertRule, snapshot: Snapshot, processes: ProcessSample[]): Target[] {
  const sys = (value: number): Target[] => [{ key: `${rule.id}:sys`, pid: null, procName: null, value }];
  switch (rule.metric) {
    case 'cpu': return processes.map((p) => ({ key: `${rule.id}:${p.pid}`, pid: p.pid, procName: p.name, value: p.cpuPct }));
    case 'commit': return processes.map((p) => ({ key: `${rule.id}:${p.pid}`, pid: p.pid, procName: p.name, value: p.commitMb }));
    case 'growth': return processes.map((p) => ({ key: `${rule.id}:${p.pid}`, pid: p.pid, procName: p.name, value: p.growthMbps }));
    case 'pagefile': return sys(snapshot.memory.pfPct);
    case 'phys': return sys(snapshot.memory.physPct);
    case 'gpu': return snapshot.gpu.utilizationPct === null ? [] : sys(snapshot.gpu.utilizationPct);
    default: return [];
  }
}

/** Evaluate all enabled rules against the newest sample. Returns newly created alerts. */
export function evaluateRules(snapshot: Snapshot, processes: ProcessSample[]): AlertEvent[] {
  const rules = listRules().filter((r) => r.enabled);
  const whitelist = listWhitelist();
  const created: AlertEvent[] = [];

  for (const rule of rules) {
    const targets = targetsFor(rule, snapshot, processes);
    for (const target of targets) {
      if (target.procName && matchesWhitelist(target.procName, whitelist)) {
        state.delete(target.key);
        continue;
      }
      const hit = rule.op === 'gt' ? target.value > rule.threshold : target.value < rule.threshold;
      const prev = state.get(target.key) ?? { count: 0, lastFired: 0 };
      prev.count = hit ? prev.count + 1 : 0;
      state.set(target.key, prev);

      const cooledDown = snapshot.ts - prev.lastFired > rule.cooldownSec * 1000;
      if (prev.count >= rule.consecutive && cooledDown) {
        prev.count = 0;
        prev.lastFired = snapshot.ts;
        const meta = METRIC_META[rule.metric];
        const subject = target.procName ? `[${target.procName}] ` : '';
        const relation = rule.op === 'gt' ? '超过' : '低于';
        const message = `${subject}${meta.label} ${target.value.toFixed(1)}${meta.unit} ${relation}阈值 ${rule.threshold}${meta.unit}`;
        created.push(insertAlert({
          ts: snapshot.ts, ruleId: rule.id, type: rule.metric, severity: rule.severity,
          pid: target.pid, procName: target.procName,
          value: target.value, threshold: rule.threshold, message,
        }));
      }
    }
  }

  // Drop state for rules that were removed/disabled.
  const activePrefixes = new Set(rules.map((r) => r.id));
  for (const key of state.keys()) {
    if (!activePrefixes.has(key.split(':')[0])) state.delete(key);
  }

  return created;
}

export function severityRank(s: AlertEvent['severity']): number {
  return s === 'critical' ? 2 : s === 'warning' ? 1 : 0;
}

export function resetRuleState(): void {
  state.clear();
}
