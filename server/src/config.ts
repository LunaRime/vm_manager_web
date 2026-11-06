import { z } from 'zod';
import type { Settings } from './types.js';

export const DEFAULT_SETTINGS: Settings = {
  sampleIntervalSec: 30,
  port: 18080,
  language: 'zh-CN',
  theme: 'light',
  retentionDays: 30,
  notifyWeb: true,
  notifySound: false,
  notifyDesktop: false,
  notifyMinSeverity: 'warning',
  autoClean: false,
  autoStart: false,
  growthThresholdMbps: 10,
};

export const settingsSchema = z.object({
  sampleIntervalSec: z.number().int().min(5).max(3600),
  port: z.number().int().min(1024).max(65535),
  language: z.enum(['zh-CN', 'zh-TW', 'en']),
  theme: z.enum(['light', 'dark']),
  retentionDays: z.number().int().min(1).max(365),
  notifyWeb: z.boolean(),
  notifySound: z.boolean(),
  notifyDesktop: z.boolean(),
  notifyMinSeverity: z.enum(['info', 'warning', 'critical']),
  autoClean: z.boolean(),
  autoStart: z.boolean(),
  growthThresholdMbps: z.number().min(1).max(1024),
}).strict();

export type SettingsPatch = z.infer<typeof settingsSchema>;

export const SETTINGS_META = [
  { key: 'sampleIntervalSec', label: '采样间隔（秒）', min: 5, max: 3600, hot: true, group: '采集' },
  { key: 'port', label: '服务端口', min: 1024, max: 65535, hot: false, group: '服务' },
  { key: 'language', label: '界面语言', hot: true, group: '外观' },
  { key: 'theme', label: '主题', hot: true, group: '外观' },
  { key: 'retentionDays', label: '历史保留天数', min: 1, max: 365, hot: true, group: '数据' },
  { key: 'notifyWeb', label: '网页通知', hot: true, group: '通知' },
  { key: 'notifySound', label: '提示音', hot: true, group: '通知' },
  { key: 'notifyDesktop', label: 'Windows 桌面通知', hot: true, group: '通知' },
  { key: 'notifyMinSeverity', label: '最低通知级别', hot: true, group: '通知' },
  { key: 'autoClean', label: '自动清理过期数据', hot: true, group: '数据' },
  { key: 'autoStart', label: '开机自启', hot: false, group: '服务' },
  { key: 'growthThresholdMbps', label: '可疑增长阈值 (MB/s)', min: 1, max: 1024, hot: true, group: '告警' },
] as const;
