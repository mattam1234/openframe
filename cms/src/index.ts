import { loadConfig } from './config';
import { DeviceRegistry } from './registry';
import { MqttBridge } from './mqtt';
import { createServer, resolveTargets, fanOut } from './server';
import { JsonStore } from './store';
import { Job, JobStore, JobScheduler } from './jobs';
import { TemplateStore, Template } from './templates';
import { HistoryStore, Sample } from './history';
import { DeviceLogStore } from './logs';
import { AlertManager } from './alerts';
import { FirmwareStore } from './firmware-store';
import { AuditLog, AuditEntry } from './audit';
import { notifyAlert } from './notifier';
import { Device } from './types';

const cfg = loadConfig();

const store = new JsonStore<{ devices: Device[] }>(cfg.dataDir, 'devices.json');
const registry = new DeviceRegistry(cfg.offlineTimeoutMs);

const saved = store.load();
if (saved?.devices) {
  registry.loadSnapshot(saved.devices);
  console.log(`[cms] restored ${saved.devices.length} device(s) from snapshot`);
}

const templates = new TemplateStore(new JsonStore<{ templates: Template[] }>(cfg.dataDir, 'templates.json'));
const history = new HistoryStore(
  new JsonStore<{ series: Record<string, Sample[]> }>(cfg.dataDir, 'history.json'),
  cfg.historyMaxSamples,
);
const alerts = new AlertManager({ lowHeapBytes: cfg.alertLowHeapBytes, lowRssiDbm: cfg.alertLowRssiDbm });
const firmware = new FirmwareStore(cfg.dataDir);
const audit = new AuditLog(new JsonStore<{ entries: AuditEntry[] }>(cfg.dataDir, 'audit.json'));

if (cfg.alertWebhookUrl) {
  const hook = cfg.alertWebhookUrl;
  alerts.on('alert', (a) => { void notifyAlert(hook, a); });
  console.log(`[cms] alerts -> webhook ${hook}`);
}

// Rolling per-device window of the Warning+ logs devices mirror over MQTT (#83).
const logs = new DeviceLogStore();

const bridge = new MqttBridge(cfg, registry, logs);

// Scheduled fleet jobs (#63): run a fleet command on a schedule. The runner
// resolves the job's target set and fans the command out via the broker.
const jobs = new JobStore(new JsonStore<{ jobs: Job[] }>(cfg.dataDir, 'jobs.json'));
const scheduler = new JobScheduler(jobs, async (job) => {
  const ids = resolveTargets(registry, job.target);
  if (!ids.length) return { ok: 0, failed: 0 };
  const results = await fanOut(bridge, ids, job.command.type, job.command.payload);
  const ok = results.filter((r) => r.ok).length;
  return { ok, failed: results.length - ok };
});
scheduler.start();

// On every fleet-state change: persist, record telemetry, and re-evaluate alerts.
registry.on('change', (device) => {
  store.saveDebounced(() => ({ devices: registry.list() }));
  if (history.record(device)) history.persist();
  alerts.evaluate(device);
});

// Periodically expire stale devices.
const sweep = setInterval(() => registry.sweep(), Math.min(cfg.offlineTimeoutMs, 15_000));
sweep.unref?.();

const server = createServer(registry, bridge, templates, history, alerts, firmware, cfg.publicUrl, cfg.authToken, cfg.viewerToken, audit, jobs, scheduler, logs);
if (cfg.authToken) console.log('[cms] token auth enabled');
if (cfg.viewerToken) console.log('[cms] read-only viewer token enabled');

// Graceful shutdown: stop accepting connections, disconnect MQTT cleanly, exit.
let shuttingDown = false;
function shutdown(signal: string): void {
  if (shuttingDown) return;
  shuttingDown = true;
  console.log(`[cms] ${signal} received — shutting down`);
  server.close(() => { bridge.close(); process.exit(0); });
  // Don't hang forever if a connection is stuck.
  setTimeout(() => process.exit(0), 3000).unref();
}
process.on('SIGINT', () => shutdown('SIGINT'));
process.on('SIGTERM', () => shutdown('SIGTERM'));
server.listen(cfg.port, () => {
  console.log(`[cms] listening on http://0.0.0.0:${cfg.port}`);
  console.log(`[cms] mqtt=${cfg.mqttUrl} baseTopic=${cfg.baseTopic} offlineTimeout=${cfg.offlineTimeoutMs}ms`);
});
