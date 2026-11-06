import { execFile } from 'node:child_process';
import { promisify } from 'node:util';
import type { AlertEvent } from './types.js';

const execFileAsync = promisify(execFile);

const TOAST_SCRIPT = String.raw`
$ErrorActionPreference = 'SilentlyContinue'
try {
  [Windows.UI.Notifications.ToastNotificationManager, Windows.UI.Notifications, ContentType = WindowsRuntime] | Out-Null
  $template = [Windows.UI.Notifications.ToastNotificationManager]::GetTemplateContent([Windows.UI.Notifications.ToastTemplateType]::ToastText02)
  $nodes = $template.GetElementsByTagName('text')
  $nodes.Item(0).AppendChild($template.CreateTextNode($env:VM_TOAST_TITLE)) | Out-Null
  $nodes.Item(1).AppendChild($template.CreateTextNode($env:VM_TOAST_BODY)) | Out-Null
  $toast = [Windows.UI.Notifications.ToastNotification]::new($template)
  $appId = '{1AC14E77-02E7-4E5D-B744-2EB1AE5198B7}\WindowsPowerShell\v1.0.0\powershell.exe'
  [Windows.UI.Notifications.ToastNotificationManager]::CreateToastNotifier($appId).Show($toast)
} catch { }
`;

/** Best-effort Windows 10+ toast; failures are intentionally swallowed. */
export async function sendDesktopNotification(alert: AlertEvent): Promise<void> {
  if (process.platform !== 'win32') return;
  const encoded = Buffer.from(TOAST_SCRIPT, 'utf16le').toString('base64');
  try {
    await execFileAsync('powershell.exe', [
      '-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass', '-EncodedCommand', encoded,
    ], {
      timeout: 10_000,
      windowsHide: true,
      env: {
        ...process.env,
        VM_TOAST_TITLE: `VM Manager · ${alert.severity.toUpperCase()}`,
        VM_TOAST_BODY: alert.message,
      },
    });
  } catch {
    // Toast is optional — never block the monitor loop.
  }
}
