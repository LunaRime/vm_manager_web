import { execFile } from 'node:child_process';
import { promisify } from 'node:util';
import type {
  GpuInfo, ProcessSample, RawProbe, RawProcess, Snapshot, SuspiciousProcess,
} from './types.js';

const execFileAsync = promisify(execFile);

/** One PowerShell probe returns every Windows metric in a single JSON document. */
const PROBE_SCRIPT = String.raw`
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
$ErrorActionPreference = 'SilentlyContinue'
$ProgressPreference = 'SilentlyContinue'

$os = Get-CimInstance -ClassName Win32_OperatingSystem
if (-not $os) { $os = @{ TotalVisibleMemorySize = 0; FreePhysicalMemory = 0; TotalVirtualMemorySize = 0; FreeVirtualMemory = 0 } }
$cpuInfo = Get-CimInstance -ClassName Win32_Processor | Measure-Object -Property NumberOfLogicalProcessors -Sum
$logical = [int]$cpuInfo.Sum
if ($logical -lt 1) { $logical = [int][Environment]::ProcessorCount }
if ($logical -lt 1) { $logical = 1 }

$pfList = @(Get-CimInstance -ClassName Win32_PageFileUsage)
$pfTotal = [double]0
$pfUsed = [double]0
foreach ($p in $pfList) {
  $pfTotal += [double]$p.AllocatedBaseSize
  $pfUsed += [double]$p.CurrentUsage
}

$gpuName = $null
$vramTotal = $null
$vc = @(Get-CimInstance -ClassName Win32_VideoController)
if ($vc.Count -gt 0) { $gpuName = [string]$vc[0].Name }
$gpuPct = $null
$vramUsed = $null
$gpuSource = 'unavailable'

# nvidia-smi is authoritative for NVIDIA cards (accurate VRAM and utilization).
try {
  $nv = & nvidia-smi --query-gpu=name,utilization.gpu,memory.used,memory.total --format=csv,noheader,nounits 2>$null
  if ($nv) {
    $line = [string]($nv | Select-Object -First 1)
    $parts = $line.Split(',')
    if ($parts.Count -ge 4) {
      $gpuName = $parts[0].Trim()
      $gpuPct = [double]$parts[1].Trim()
      $vramUsed = [double]$parts[2].Trim()
      $vramTotal = [double]$parts[3].Trim()
      $gpuSource = 'nvidia-smi'
    }
  }
} catch { }

# PDH fallback for non-NVIDIA GPUs.
if ($null -eq $gpuPct) {
  try {
    $samples = (Get-Counter -Counter '\GPU Engine(*)\Utilization Percentage' -SampleInterval 1 -MaxSamples 1 -ErrorAction Stop).CounterSamples
    if ($samples) {
      $maxVal = ($samples | Measure-Object -Property CookedValue -Maximum).Maximum
      $gpuPct = [math]::Round([double]$maxVal, 1)
      $gpuSource = 'pdh'
    }
  } catch { }
}
if ($null -eq $vramUsed) {
  try {
    $memSamples = (Get-Counter -Counter '\GPU Process Memory(*)\Dedicated Usage' -SampleInterval 1 -MaxSamples 1 -ErrorAction Stop).CounterSamples
    if ($memSamples) {
      $sumVal = ($memSamples | Measure-Object -Property CookedValue -Sum).Sum
      $candidate = [math]::Round([double]$sumVal / 1MB, 0)
      if ($candidate -gt 0 -and $candidate -lt 65536) { $vramUsed = $candidate }
    }
  } catch { }
}
if ($null -eq $vramTotal -and $vc.Count -gt 0) {
  $adapterMb = [math]::Round([double]$vc[0].AdapterRAM / 1MB, 0)
  # AdapterRAM is a 32-bit value and saturates near 4 GB — discard unreliable readings.
  if ($adapterMb -ge 128 -and $adapterMb -lt 4000) { $vramTotal = $adapterMb }
}

$idleSec = 0
try {
  Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class VMIdleInfo {
  [StructLayout(LayoutKind.Sequential)]
  public struct LASTINPUTINFO { public uint cbSize; public uint dwTime; }
  [DllImport("user32.dll")] public static extern bool GetLastInputInfo(ref LASTINPUTINFO plii);
  [DllImport("kernel32.dll")] public static extern uint GetTickCount();
  public static uint Seconds() {
    LASTINPUTINFO lii = new LASTINPUTINFO();
    lii.cbSize = (uint)Marshal.SizeOf(lii);
    if (!GetLastInputInfo(ref lii)) return 0;
    return (GetTickCount() - lii.dwTime) / 1000;
  }
}
'@
  $idleSec = [double][VMIdleInfo]::Seconds()
} catch { }

$procs = New-Object System.Collections.ArrayList
foreach ($p in @(Get-CimInstance -ClassName Win32_Process)) {
  $created = [double]0
  try {
    if ($p.CreationDate) {
      $created = [double](([datetime]$p.CreationDate).ToUniversalTime() - [datetime]'1970-01-01T00:00:00Z').TotalMilliseconds
    }
  } catch { }
  [void]$procs.Add([pscustomobject]@{
    pid = [int]$p.ProcessId
    name = [string]$p.Name
    wsKb = [double]$p.WorkingSetSize / 1KB
    commitKb = [double]$p.PageFileUsage
    kernelTime = [double]$p.KernelModeTime
    userTime = [double]$p.UserModeTime
    path = [string]$p.ExecutablePath
    createdAt = $created
  })
}

$result = [ordered]@{
  memory = [ordered]@{
    totalPhysKb = [double]$os.TotalVisibleMemorySize
    availPhysKb = [double]$os.FreePhysicalMemory
    totalVirtualKb = [double]$os.TotalVirtualMemorySize
    freeVirtualKb = [double]$os.FreeVirtualMemory
    pfTotalMb = $pfTotal
    pfUsedMb = $pfUsed
  }
  logicalProcessors = $logical
  idleSec = $idleSec
  gpu = [ordered]@{
    name = $gpuName
    utilizationPct = $gpuPct
    vramUsedMb = $vramUsed
    vramTotalMb = $vramTotal
    source = $gpuSource
  }
  processes = @($procs)
}
$result | ConvertTo-Json -Depth 6 -Compress
`;

interface CpuMark { kernel: number; user: number; ts: number }
interface GrowthMark {
  commitMb: number; ts: number; firstSeen: number;
  peak: number; count: number; lastRate: number;
}

const cpuMarks = new Map<number, CpuMark>();
const growthMarks = new Map<number, GrowthMark>();
const SERVER_STARTED = Date.now();

const clamp = (v: number, lo: number, hi: number): number => Math.min(hi, Math.max(lo, v));
const round = (v: number, digits = 2): number => Number(v.toFixed(digits));
const finite = (v: unknown, fallback = 0): number => {
  const n = Number(v);
  return Number.isFinite(n) ? n : fallback;
};

export interface CollectResult {
  snapshot: Snapshot;
  processes: ProcessSample[];
  suspicious: SuspiciousProcess[];
}

function parseProbe(stdout: string): RawProbe {
  const clean = stdout.replace(/^\uFEFF/, '');
  const start = clean.indexOf('{');
  const end = clean.lastIndexOf('}');
  if (start < 0 || end <= start) throw new Error('PowerShell 未返回有效 JSON');
  const parsed = JSON.parse(clean.slice(start, end + 1)) as Record<string, any>;
  const mem = parsed.memory ?? {};
  const gpu = parsed.gpu ?? null;
  const rawProcesses = Array.isArray(parsed.processes) ? parsed.processes : [];
  const processes: RawProcess[] = rawProcesses.map((p: Record<string, unknown>) => ({
    pid: finite(p.pid), name: String(p.name ?? 'unknown'),
    wsKb: finite(p.wsKb), commitKb: finite(p.commitKb),
    kernelTime: finite(p.kernelTime), userTime: finite(p.userTime),
    path: p.path ? String(p.path) : null,
    createdAt: finite(p.createdAt) || null,
  }));
  return {
    memory: {
      totalPhysKb: finite(mem.totalPhysKb), availPhysKb: finite(mem.availPhysKb),
      totalVirtualKb: finite(mem.totalVirtualKb), freeVirtualKb: finite(mem.freeVirtualKb),
      pfTotalMb: finite(mem.pfTotalMb), pfUsedMb: finite(mem.pfUsedMb),
    },
    logicalProcessors: Math.max(1, finite(parsed.logicalProcessors, 1)),
    idleSec: finite(parsed.idleSec),
    gpu: gpu ? {
      name: gpu.name ? String(gpu.name) : null,
      utilizationPct: gpu.utilizationPct === null || gpu.utilizationPct === undefined ? null : finite(gpu.utilizationPct),
      vramUsedMb: gpu.vramUsedMb === null || gpu.vramUsedMb === undefined ? null : finite(gpu.vramUsedMb),
      vramTotalMb: gpu.vramTotalMb === null || gpu.vramTotalMb === undefined ? null : finite(gpu.vramTotalMb),
      source: String(gpu.source ?? 'unknown'),
    } : null,
    processes,
  };
}

async function probeWindows(): Promise<RawProbe> {
  const encoded = Buffer.from(PROBE_SCRIPT, 'utf16le').toString('base64');
  const { stdout } = await execFileAsync('powershell.exe', [
    '-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass', '-EncodedCommand', encoded,
  ], { timeout: 30_000, maxBuffer: 16 * 1024 * 1024, windowsHide: true });
  return parseProbe(stdout);
}

/** Deterministic synthetic probe for UI/CI verification. Never used unless VM_DEMO=1. */
function probeDemo(): RawProbe {
  const elapsed = (Date.now() - SERVER_STARTED) / 1000;
  const wave = Math.sin(elapsed / 17);
  const totalPhysKb = 16 * 1024 * 1024;
  const availPhysKb = totalPhysKb * (0.38 + 0.07 * wave);
  const pfTotalMb = 24 * 1024;
  const pfUsedMb = pfTotalMb * (0.62 + 0.09 * wave);
  const logical = 8;
  const names = [
    'chrome.exe', 'msedge.exe', 'Code.exe', 'node.exe', 'explorer.exe', 'dwm.exe',
    'SearchHost.exe', 'Teams.exe', 'WeChat.exe', 'svchost.exe', 'powershell.exe',
    'vm_manager.exe', 'python.exe', 'docker.exe', 'idea64.exe', 'Spotify.exe',
    'OneDrive.exe', 'RuntimeBroker.exe', 'conhost.exe', 'csrss.exe', 'services.exe',
    'MemoryLeakDemo.exe', 'notepad.exe', 'Taskmgr.exe',
  ];
  const processes: RawProcess[] = names.map((name, i) => {
    const cpuTarget = name === 'MemoryLeakDemo.exe' ? 34 : (3 + ((i * 7) % 19));
    const baseCommit = name === 'MemoryLeakDemo.exe' ? 420 : 40 + ((i * 53) % 900);
    const growth = name === 'MemoryLeakDemo.exe' ? elapsed * 18 : 0;
    return {
      pid: 1000 + i * 37, name,
      wsKb: (baseCommit * 0.78 + growth * 0.8) * 1024,
      commitKb: (baseCommit + growth) * 1024,
      kernelTime: elapsed * (cpuTarget / 100) * logical * 1e7 * 0.6,
      userTime: elapsed * (cpuTarget / 100) * logical * 1e7 * 0.4,
      path: `C:\\Program Files\\Demo\\${name}`,
      createdAt: SERVER_STARTED - (i + 1) * 3600_000,
    };
  });
  return {
    memory: {
      totalPhysKb, availPhysKb,
      totalVirtualKb: totalPhysKb + pfTotalMb * 1024,
      freeVirtualKb: availPhysKb + (pfTotalMb - pfUsedMb) * 1024,
      pfTotalMb, pfUsedMb,
    },
    logicalProcessors: logical,
    idleSec: Math.max(0, Math.round(elapsed % 420)),
    gpu: {
      name: 'NVIDIA GeForce RTX 4070 (demo)',
      utilizationPct: round(clamp(42 + 24 * wave, 0, 100), 1),
      vramUsedMb: Math.round(4200 + 800 * wave),
      vramTotalMb: 12288,
      source: 'demo',
    },
    processes,
  };
}

function buildResult(probe: RawProbe, demo: boolean, growthThreshold: number, whitelistMatch: (name: string) => boolean): CollectResult {
  const ts = Date.now();
  const mem = probe.memory;
  const totalPhysMb = round(mem.totalPhysKb / 1024);
  const availPhysMb = round(mem.availPhysKb / 1024);
  let totalPfMb = round(mem.pfTotalMb);
  if (totalPfMb <= 0) totalPfMb = round(Math.max(0, (mem.totalVirtualKb - mem.totalPhysKb) / 1024));
  const usedPfMb = round(clamp(mem.pfUsedMb, 0, totalPfMb));
  const availPfMb = round(Math.max(0, totalPfMb - usedPfMb));
  const physPct = round(clamp(totalPhysMb > 0 ? (1 - availPhysMb / totalPhysMb) * 100 : 0, 0, 100));
  const pfPct = round(clamp(totalPfMb > 0 ? (usedPfMb / totalPfMb) * 100 : 0, 0, 100));

  const gpuRaw = probe.gpu;
  const gpu: GpuInfo = {
    available: !!gpuRaw && gpuRaw.utilizationPct !== null,
    utilizationPct: gpuRaw?.utilizationPct ?? null,
    vramUsedMb: gpuRaw?.vramUsedMb ?? null,
    vramTotalMb: gpuRaw?.vramTotalMb ?? null,
    name: gpuRaw?.name ?? null,
    source: gpuRaw?.source ?? 'unavailable',
  };

  const seenPids = new Set<number>();
  const processes: ProcessSample[] = [];
  let cpuSum = 0;
  let idlePct = 0;

  for (const raw of probe.processes) {
    seenPids.add(raw.pid);
    const commitMb = round(raw.commitKb / 1024);
    const wsMb = round(raw.wsKb / 1024);

    const prevCpu = cpuMarks.get(raw.pid);
    let cpuPct = 0;
    if (prevCpu) {
      const dt = (ts - prevCpu.ts) / 1000;
      if (dt > 0.2) {
        const deltaSec = ((raw.kernelTime - prevCpu.kernel) + (raw.userTime - prevCpu.user)) / 1e7;
        if (deltaSec >= 0) cpuPct = round(clamp((deltaSec / (dt * probe.logicalProcessors)) * 100, 0, 100));
      }
    }
    cpuMarks.set(raw.pid, { kernel: raw.kernelTime, user: raw.userTime, ts });
    // PID 0 is "System Idle Process": its delta is idle time, not CPU load.
    if (raw.pid === 0) {
      idlePct = cpuPct;
      continue;
    }
    cpuSum += cpuPct;

    const prevGrowth = growthMarks.get(raw.pid);
    let growthMbps = 0, peakGrowthMbps = 0, suspiciousCount = 0, firstSeen = ts;
    if (prevGrowth) {
      const dt = (ts - prevGrowth.ts) / 1000;
      // A minimum 3s window suppresses noise from very short (manual) sample intervals.
      if (dt >= 3 && commitMb >= 100) {
        const rate = (commitMb - prevGrowth.commitMb) / dt;
        growthMbps = round(rate);
        peakGrowthMbps = round(Math.max(prevGrowth.peak, rate));
        suspiciousCount = rate > growthThreshold ? prevGrowth.count + 1 : 0;
      } else {
        peakGrowthMbps = prevGrowth.peak;
      }
      firstSeen = prevGrowth.firstSeen;
    }
    growthMarks.set(raw.pid, {
      commitMb, ts, firstSeen, peak: peakGrowthMbps,
      count: suspiciousCount, lastRate: growthMbps,
    });

    processes.push({
      pid: raw.pid, name: raw.name, commitMb, wsMb, cpuPct,
      growthMbps: Math.max(0, growthMbps), peakGrowthMbps: Math.max(0, peakGrowthMbps),
      suspiciousCount, path: raw.path, createdAt: raw.createdAt,
      whitelisted: whitelistMatch(raw.name),
    });
  }

  for (const pid of [...cpuMarks.keys()]) if (!seenPids.has(pid)) cpuMarks.delete(pid);
  for (const pid of [...growthMarks.keys()]) if (!seenPids.has(pid)) growthMarks.delete(pid);

  processes.sort((a, b) => b.commitMb - a.commitMb);

  const snapshot: Snapshot = {
    ts,
    memory: {
      totalPhysMb, availPhysMb, physPct, totalPfMb, availPfMb, pfPct,
      totalVirtualMb: round(mem.totalVirtualKb / 1024),
      freeVirtualMb: round(mem.freeVirtualKb / 1024),
    },
    cpuTotalPct: round(clamp(idlePct > 0 ? Math.max(0, 100 - idlePct) : cpuSum, 0, 100)),
    gpu,
    idleSec: Math.round(probe.idleSec),
    processCount: processes.length,
    uptimeSec: Math.round((ts - SERVER_STARTED) / 1000),
    demo,
  };

  const suspicious: SuspiciousProcess[] = processes
    .filter((p) => p.suspiciousCount > 0 || p.peakGrowthMbps > growthThreshold)
    .map((p) => ({
      pid: p.pid, name: p.name, growthMbps: p.growthMbps,
      peakGrowthMbps: p.peakGrowthMbps,
      firstSeen: growthMarks.get(p.pid)?.firstSeen ?? ts,
      alerts: p.suspiciousCount, commitMb: p.commitMb, whitelisted: p.whitelisted,
    }))
    .sort((a, b) => b.peakGrowthMbps - a.peakGrowthMbps);

  return { snapshot, processes, suspicious };
}

export async function collectOnce(options: {
  demo: boolean;
  growthThresholdMbps: number;
  whitelistMatch: (name: string) => boolean;
}): Promise<CollectResult> {
  let probe: RawProbe;
  if (options.demo) {
    probe = probeDemo();
  } else {
    if (process.platform !== 'win32') {
      throw new Error('当前平台不是 Windows；如需演示请设置 VM_DEMO=1');
    }
    probe = await probeWindows();
  }
  return buildResult(probe, options.demo, options.growthThresholdMbps, options.whitelistMatch);
}

export function collectorUptimeSec(): number {
  return Math.round((Date.now() - SERVER_STARTED) / 1000);
}
