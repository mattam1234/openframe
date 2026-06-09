import express from 'express';
import http from 'http';
import path from 'path';
import multer from 'multer';
import { WebSocketServer, WebSocket } from 'ws';
import { DeviceRegistry } from './registry';
import { MqttBridge } from './mqtt';
import { TemplateStore } from './templates';
import { HistoryStore } from './history';
import { AlertManager } from './alerts';
import { FirmwareStore } from './firmware-store';

interface TargetSpec {
  deviceIds?: unknown;
  tags?: unknown;
  onlineOnly?: unknown;
}

// Resolve the set of device ids a bulk/deploy request targets. Precedence:
// explicit deviceIds → devices matching any of `tags` → the whole fleet. The
// onlineOnly flag further filters the tag/fleet cases.
function resolveTargets(registry: DeviceRegistry, body: TargetSpec): string[] {
  if (Array.isArray(body.deviceIds) && body.deviceIds.length) {
    return body.deviceIds.filter((id): id is string => typeof id === 'string');
  }
  let list = registry.list();
  if (body.onlineOnly) list = list.filter((d) => d.online);
  if (Array.isArray(body.tags) && body.tags.length) {
    const wanted = new Set(body.tags.filter((t): t is string => typeof t === 'string'));
    list = list.filter((d) => d.tags.some((t) => wanted.has(t)));
  }
  return list.map((d) => d.deviceId);
}

// Fan a command out to many devices concurrently; one device failing (e.g. a
// timeout) never rejects the whole batch — it becomes a per-device error entry.
async function fanOut(
  bridge: MqttBridge,
  deviceIds: string[],
  type: string,
  payload: unknown,
): Promise<Array<Record<string, unknown>>> {
  return Promise.all(
    deviceIds.map(async (deviceId) => {
      try {
        const result = await bridge.sendCommand(deviceId, { type, payload });
        return { deviceId, ...result };
      } catch (err) {
        return { deviceId, ok: false, error: err instanceof Error ? err.message : 'failed' };
      }
    }),
  );
}

export function createServer(
  registry: DeviceRegistry,
  bridge: MqttBridge,
  templates: TemplateStore,
  history: HistoryStore,
  alerts: AlertManager,
  firmware: FirmwareStore,
  publicUrl?: string,
): http.Server {
  const app = express();
  app.use(express.json({ limit: '256kb' }));
  app.use(express.static(path.join(__dirname, '..', 'public')));

  const upload = multer({
    storage: multer.diskStorage({
      destination: (_req, _file, cb) => cb(null, firmware.dir),
      filename: (_req, file, cb) => cb(null, FirmwareStore.sanitize(file.originalname)),
    }),
    limits: { fileSize: 8 * 1024 * 1024 },
  });

  app.get('/api/health', (_req, res) => {
    res.json({ ok: true, mqtt: bridge.connected, devices: registry.list().length });
  });

  // ── Devices ────────────────────────────────────────────────────────────────
  app.get('/api/devices', (_req, res) => {
    res.json({ devices: registry.list() });
  });

  app.get('/api/devices/:id', (req, res) => {
    const device = registry.get(req.params.id);
    if (!device) {
      res.status(404).json({ error: 'unknown device' });
      return;
    }
    res.json(device);
  });

  app.get('/api/devices/:id/history', (req, res) => {
    res.json({ deviceId: req.params.id, samples: history.get(req.params.id) });
  });

  // Set a device's tags (operator-assigned, for grouping/bulk targeting).
  app.put('/api/devices/:id/tags', (req, res) => {
    const tags = (req.body ?? {}).tags;
    if (!Array.isArray(tags)) {
      res.status(400).json({ error: 'tags must be an array' });
      return;
    }
    if (!registry.setTags(req.params.id, tags)) {
      res.status(404).json({ error: 'unknown device' });
      return;
    }
    res.json(registry.get(req.params.id));
  });

  // ── Alerts ───────────────────────────────────────────────────────────────────
  app.get('/api/alerts', (_req, res) => {
    res.json({ active: alerts.activeAlerts(), recent: alerts.recentAlerts() });
  });

  // Single-device command: publish to <base>/<id>/cmd and await the ack.
  app.post('/api/devices/:id/cmd', async (req, res) => {
    const { type, payload } = req.body ?? {};
    if (typeof type !== 'string' || !type) {
      res.status(400).json({ error: 'a string "type" is required' });
      return;
    }
    try {
      const result = await bridge.sendCommand(req.params.id, { type, payload });
      res.json(result);
    } catch (err) {
      res.status(504).json({ error: err instanceof Error ? err.message : 'command failed' });
    }
  });

  // ── Bulk command ─────────────────────────────────────────────────────────────
  // Send one command to many devices. Body: { type, payload?, deviceIds?, onlineOnly? }
  app.post('/api/commands', async (req, res) => {
    const body = req.body ?? {};
    const { type, payload } = body;
    if (typeof type !== 'string' || !type) {
      res.status(400).json({ error: 'a string "type" is required' });
      return;
    }
    const targets = resolveTargets(registry, body);
    if (!targets.length) {
      res.status(400).json({ error: 'no target devices' });
      return;
    }
    const results = await fanOut(bridge, targets, type, payload);
    res.json({ count: results.length, ok: results.filter((r) => r.ok).length, results });
  });

  // ── Template library ─────────────────────────────────────────────────────────
  app.get('/api/templates', (_req, res) => {
    res.json({ templates: templates.list() });
  });

  app.post('/api/templates', (req, res) => {
    const t = templates.upsert(req.body ?? {});
    if (!t) {
      res.status(400).json({ error: 'name and command.type are required' });
      return;
    }
    res.status(201).json(t);
  });

  app.put('/api/templates/:id', (req, res) => {
    const t = templates.upsert({ ...(req.body ?? {}), id: req.params.id });
    if (!t) {
      res.status(400).json({ error: 'name and command.type are required' });
      return;
    }
    res.json(t);
  });

  app.delete('/api/templates/:id', (req, res) => {
    res.json({ removed: templates.remove(req.params.id) });
  });

  // Deploy a template to a set of devices. Body: { deviceIds?, onlineOnly? }
  app.post('/api/templates/:id/deploy', async (req, res) => {
    const template = templates.get(req.params.id);
    if (!template) {
      res.status(404).json({ error: 'unknown template' });
      return;
    }
    const targets = resolveTargets(registry, req.body ?? {});
    if (!targets.length) {
      res.status(400).json({ error: 'no target devices' });
      return;
    }
    const results = await fanOut(bridge, targets, template.command.type, template.command.payload);
    res.json({ template: template.id, count: results.length, ok: results.filter((r) => r.ok).length, results });
  });

  // ── Fleet OTA: firmware hosting + deploy ─────────────────────────────────────
  app.get('/api/firmware', (_req, res) => {
    res.json({ files: firmware.list() });
  });

  app.post('/api/firmware', upload.single('file'), (req, res) => {
    if (!req.file) {
      res.status(400).json({ error: 'no file uploaded (field "file")' });
      return;
    }
    res.status(201).json({ name: req.file.filename, size: req.file.size });
  });

  app.delete('/api/firmware/:name', (req, res) => {
    res.json({ removed: firmware.remove(req.params.name) });
  });

  // Devices download the image from here over plain HTTP (trusted LAN).
  app.get('/firmware/:name', (req, res) => {
    const name = FirmwareStore.sanitize(req.params.name);
    if (!firmware.has(name)) {
      res.status(404).json({ error: 'unknown firmware' });
      return;
    }
    res.type('application/octet-stream').sendFile(path.resolve(firmware.dir, name));
  });

  // Tell target devices to OTA from this CMS's firmware URL.
  app.post('/api/firmware/:name/deploy', async (req, res) => {
    const name = FirmwareStore.sanitize(req.params.name);
    if (!firmware.has(name)) {
      res.status(404).json({ error: 'unknown firmware' });
      return;
    }
    // SECURITY: the OTA URL must NOT be derived from the request Host header — a
    // spoofed/rebound Host would make the whole fleet flash attacker-hosted
    // firmware (remote code execution). Require an explicitly configured base URL.
    if (!publicUrl) {
      res.status(500).json({ error: 'CMS_PUBLIC_URL must be configured before deploying firmware' });
      return;
    }
    const targets = resolveTargets(registry, req.body ?? {});
    if (!targets.length) {
      res.status(400).json({ error: 'no target devices' });
      return;
    }
    const base = publicUrl.replace(/\/+$/, '');
    const url = `${base}/firmware/${encodeURIComponent(name)}`;
    const results = await fanOut(bridge, targets, 'ota_url', { url });
    res.json({ url, count: results.length, ok: results.filter((r) => r.ok).length, results });
  });

  // ── WebSocket live updates ─────────────────────────────────────────────────
  const server = http.createServer(app);
  const wss = new WebSocketServer({ server, path: '/ws' });

  const broadcast = (obj: unknown) => {
    const msg = JSON.stringify(obj);
    for (const client of wss.clients) {
      if (client.readyState === WebSocket.OPEN) client.send(msg);
    }
  };

  wss.on('connection', (ws) => {
    ws.send(JSON.stringify({ type: 'snapshot', devices: registry.list() }));
    ws.send(JSON.stringify({ type: 'alerts', active: alerts.activeAlerts() }));
  });

  registry.on('change', (device) => broadcast({ type: 'device', device }));
  alerts.on('alert', (alert) => broadcast({ type: 'alert', alert }));

  return server;
}
