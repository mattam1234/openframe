import { loadConfig } from './config';
import { DeviceRegistry } from './registry';
import { MqttBridge } from './mqtt';
import { createServer } from './server';
import { JsonStore } from './store';
import { TemplateStore, Template } from './templates';
import { HistoryStore, Sample } from './history';
import { AlertManager } from './alerts';
import { FirmwareStore } from './firmware-store';
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

const bridge = new MqttBridge(cfg, registry);

// On every fleet-state change: persist, record telemetry, and re-evaluate alerts.
registry.on('change', (device) => {
  store.saveDebounced(() => ({ devices: registry.list() }));
  if (history.record(device)) history.persist();
  alerts.evaluate(device);
});

// Periodically expire stale devices.
const sweep = setInterval(() => registry.sweep(), Math.min(cfg.offlineTimeoutMs, 15_000));
sweep.unref?.();

const server = createServer(registry, bridge, templates, history, alerts, firmware, cfg.publicUrl);
server.listen(cfg.port, () => {
  console.log(`[cms] listening on http://0.0.0.0:${cfg.port}`);
  console.log(`[cms] mqtt=${cfg.mqttUrl} baseTopic=${cfg.baseTopic} offlineTimeout=${cfg.offlineTimeoutMs}ms`);
});
