import type { ProcessSample, Settings, Snapshot, SuspiciousProcess } from './types.js';

export interface AppContext {
  getSnapshot(): Snapshot | null;
  getProcesses(): ProcessSample[];
  getSuspicious(): SuspiciousProcess[];
  getSettings(): Settings;
  applySettings(next: Settings): void;
  getCollectorError(): string | null;
  requestSample(): Promise<void>;
  serverUptimeSec(): number;
}
