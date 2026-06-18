import express from 'express';
import http from 'http';
import path from 'path';
import fs from 'fs';
import multer from 'multer';
import { WebSocketServer, WebSocket } from 'ws';
import { DeviceRegistry } from './registry';
import { MqttBridge } from './mqtt';
import { TemplateStore } from './templates';
import { HistoryStore } from './history';
import { AlertManager } from './alerts';
import { FirmwareStore } from './firmware-store';
import { makeAuth, tokenMatches } from './auth';
import { AuditLog } from './audit';
import { RateLimiter, FailureLockout } from './rate-limit';
import { Job, JobStore, JobScheduler } from './jobs';
import { buildProvisionConfig, provisionUrl, provisionQrDataUrl } from './provision';

interface TargetSpec {
  deviceIds?: unknown;
  tags?: unknown;
  onlineOnly?: unknown;
}

// Resolve the set of device ids a bulk/deploy request targets. Precedence:
// explicit deviceIds → devices matching any of `tags` → the whole fleet. The
// onlineOnly flag further filters the tag/fleet cases.
export function resolveTargets(registry: DeviceRegistry, body: TargetSpec): string[] {
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
export async function fanOut(
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
  authToken?: string,
  viewerToken?: string,
  audit?: AuditLog,
  jobs?: JobStore,
  scheduler?: JobScheduler,
): http.Server {
  const app = express();
  const auth = makeAuth(authToken, viewerToken);
  // Role attached by auth.middleware; 'open' when auth is disabled on the LAN.
  const roleOf = (req: unknown): string =>
    (req as { role?: string }).role ?? (auth.enabled ? 'unknown' : 'open');
  app.use(express.json({ limit: '256kb' }));
  // Static UI (incl. the login overlay) is served without auth; the API below is
  // gated. Firmware binaries stay public so token-less devices can fetch them.
  app.use(express.static(path.join(__dirname, '..', 'public')));

  // Vue CMS app (#45): served at /app when it's been built (cms/web/dist). The
  // vanilla pages stay at / until the port reaches parity. Non-breaking — if the
  // build isn't present (e.g. before `npm --prefix web run build`), /app is simply
  // absent. The SPA fallback lets client-side routes deep-link.
  const webDist = path.join(__dirname, '..', 'web', 'dist');
  if (fs.existsSync(path.join(webDist, 'index.html'))) {
    app.use('/app', express.static(webDist));
    app.get('/app/*', (_req, res) => res.sendFile(path.join(webDist, 'index.html')));
  }

  // ── Auth (public endpoints) ──────────────────────────────────────────────────
  app.get('/api/auth', (req, res) => {
    res.json({ authRequired: auth.enabled, authed: auth.ok(req), role: auth.role(req) });
  });
  // Throttle login attempts per client, and lock an IP out after repeated failures,
  // to make a configured token unbruteforceable (#80).
  const loginLimiter = new RateLimiter(10, 60_000);
  const loginLockout = new FailureLockout(5, 5 * 60_000);  // 5 fails → 5 min lockout
  app.post('/api/login', (req, res) => {
    if (!auth.enabled) { res.json({ ok: true, authRequired: false }); return; }
    const ip = req.ip ?? 'unknown';
    if (loginLockout.locked(ip)) {
      res.status(429).json({ ok: false, error: 'too many failed attempts — locked out, try again later' });
      return;
    }
    if (!loginLimiter.allow(ip)) {
      res.status(429).json({ ok: false, error: 'too many attempts, slow down' });
      return;
    }
    const provided = (req.body ?? {}).token;
    // Accept either the admin or the viewer token; the cookie carries whichever
    // matched, so the role is re-derived from it on every subsequent request.
    const matched = typeof provided === 'string' &&
      ((authToken && tokenMatches(provided, authToken)) ? authToken
        : (viewerToken && tokenMatches(provided, viewerToken)) ? viewerToken
        : null);
    if (matched) {
      loginLockout.reset(ip);
      res.setHeader('Set-Cookie', auth.loginCookie(matched));
      res.json({ ok: true, role: authToken && matched === authToken ? 'admin' : 'viewer' });
    } else {
      const lockedMs = loginLockout.recordFailure(ip);
      if (lockedMs > 0) {
        res.status(429).json({ ok: false, error: 'too many failed attempts — locked out, try again later' });
      } else {
        res.status(401).json({ ok: false, error: 'invalid token' });
      }
    }
  });
  app.post('/api/logout', (_req, res) => {
    res.setHeader('Set-Cookie', auth.clearCookie());
    res.json({ ok: true });
  });

  app.get('/api/health', (_req, res) => {
    res.json({ ok: true, mqtt: bridge.connected, devices: registry.list().length });
  });

  // Prometheus exposition for the whole fleet (#65). Not under /api, so it's
  // scrapable without the API token (it carries no secrets — just telemetry).
  app.get('/metrics', (_req, res) => {
    const devs = registry.list();
    const online = devs.filter((d) => d.online).length;
    const esc = (s: string) => String(s).replace(/[\\"\n]/g, '');
    const lines: string[] = [];
    const help = (name: string, type: string, helpText: string) => {
      lines.push(`# HELP ${name} ${helpText}`, `# TYPE ${name} ${type}`);
    };

    help('openframe_cms_devices_total', 'gauge', 'Devices known to the CMS.');
    lines.push(`openframe_cms_devices_total ${devs.length}`);
    help('openframe_cms_devices_online', 'gauge', 'Devices currently online.');
    lines.push(`openframe_cms_devices_online ${online}`);
    help('openframe_cms_mqtt_connected', 'gauge', '1 if the CMS broker link is up.');
    lines.push(`openframe_cms_mqtt_connected ${bridge.connected ? 1 : 0}`);

    // Per-device gauges, labelled by device id.
    const gauge = (name: string, type: string, helpText: string, pick: (d: typeof devs[number]) => number | undefined) => {
      help(name, type, helpText);
      for (const d of devs) {
        const v = pick(d);
        if (typeof v === 'number' && Number.isFinite(v)) {
          lines.push(`${name}{device="${esc(d.deviceId)}"} ${v}`);
        }
      }
    };
    gauge('openframe_device_online', 'gauge', 'Per-device online state (1/0).', (d) => (d.online ? 1 : 0));
    gauge('openframe_device_free_heap_bytes', 'gauge', 'Per-device free heap.', (d) => d.freeHeap);
    gauge('openframe_device_rssi_dbm', 'gauge', 'Per-device WiFi RSSI.', (d) => d.rssi);
    gauge('openframe_device_uptime_seconds', 'counter', 'Per-device uptime.', (d) => (d.uptimeMs != null ? Math.floor(d.uptimeMs / 1000) : undefined));
    gauge('openframe_device_cpu_load_percent', 'gauge', 'Per-device CPU load.', (d) => d.cpuLoadPercent);

    res.type('text/plain; version=0.0.4').send(lines.join('\n') + '\n');
  });

  // Everything under /api below this point requires a valid token (when enabled).
  app.use('/api', auth.middleware);

  const upload = multer({
    storage: multer.diskStorage({
      destination: (_req, _file, cb) => cb(null, firmware.dir),
      filename: (_req, file, cb) => cb(null, FirmwareStore.sanitize(file.originalname)),
    }),
    limits: { fileSize: 8 * 1024 * 1024 },
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

  // Live screen preview (#57): ask the device what its displays are currently
  // showing and relay the ack. A GET (read-only) so the viewer role can use it;
  // it drives no hardware, just snapshots the rendered widget state.
  app.get('/api/devices/:id/screens', async (req, res) => {
    try {
      const result = await bridge.sendCommand(req.params.id, { type: 'get_screens' });
      res.json(result);
    } catch (err) {
      res.status(504).json({ error: err instanceof Error ? err.message : 'command failed' });
    }
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

  app.put('/api/devices/:id/notes', (req, res) => {
    const notes = (req.body ?? {}).notes;
    if (typeof notes !== 'string') {
      res.status(400).json({ error: 'notes must be a string' });
      return;
    }
    if (notes.length > 2000) {
      res.status(400).json({ error: 'notes too long (max 2000 chars)' });
      return;
    }
    if (!registry.setNotes(req.params.id, notes)) {
      res.status(404).json({ error: 'unknown device' });
      return;
    }
    res.json(registry.get(req.params.id));
  });

  // ── Alerts ───────────────────────────────────────────────────────────────────
  app.get('/api/alerts', (_req, res) => {
    res.json({ active: alerts.activeAlerts(), recent: alerts.recentAlerts() });
  });

  // ── Audit log: who pushed what, when, and the result ─────────────────────────
  app.get('/api/audit', (_req, res) => {
    res.json({ entries: audit ? audit.list() : [] });
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
      audit?.record({ role: roleOf(req), action: 'device.cmd', target: req.params.id, detail: type, ok: !!(result as { ok?: boolean })?.ok });
      res.json(result);
    } catch (err) {
      audit?.record({ role: roleOf(req), action: 'device.cmd', target: req.params.id, detail: type, ok: false });
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
    const okCount = results.filter((r) => r.ok).length;
    audit?.record({ role: roleOf(req), action: 'commands.bulk', detail: type, count: results.length, ok: okCount === results.length });
    res.json({ count: results.length, ok: okCount, results });
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
    const okCount = results.filter((r) => r.ok).length;
    audit?.record({ role: roleOf(req), action: 'template.deploy', target: template.id, detail: template.command.type, count: results.length, ok: okCount === results.length });
    res.json({ template: template.id, count: results.length, ok: okCount, results });
  });

  // ── Scheduled fleet jobs (#63) ───────────────────────────────────────────────
  app.get('/api/jobs', (_req, res) => {
    res.json({ jobs: jobs ? jobs.list() : [] });
  });

  app.post('/api/jobs', (req, res) => {
    if (!jobs) { res.status(503).json({ error: 'jobs not available' }); return; }
    const b = req.body ?? {};
    const sched = b.schedule ?? {};
    if (typeof b.id !== 'string' || !b.id || typeof b.name !== 'string' ||
        typeof b.command?.type !== 'string' ||
        (sched.mode !== 'interval' && sched.mode !== 'daily')) {
      res.status(400).json({ error: 'id, name, command.type and a valid schedule are required' });
      return;
    }
    const job: Job = {
      id: b.id,
      name: b.name,
      enabled: b.enabled !== false,
      schedule: {
        mode: sched.mode,
        intervalMin: sched.mode === 'interval' ? Math.max(1, Number(sched.intervalMin) || 0) : undefined,
        dailyMinute: sched.mode === 'daily' ? Math.min(1439, Math.max(0, Number(sched.dailyMinute) || 0)) : undefined,
      },
      command: { type: b.command.type, payload: b.command.payload },
      target: {
        deviceIds: Array.isArray(b.target?.deviceIds) ? b.target.deviceIds : undefined,
        tags: Array.isArray(b.target?.tags) ? b.target.tags : undefined,
        onlineOnly: !!b.target?.onlineOnly,
      },
    };
    // Preserve runtime bookkeeping across edits.
    const prev = jobs.get(job.id);
    if (prev) { job.lastRun = prev.lastRun; job.lastResult = prev.lastResult; job.lastDailyDay = prev.lastDailyDay; }
    audit?.record({ role: roleOf(req), action: 'job.save', target: job.id, detail: job.command.type, ok: true });
    res.json(jobs.upsert(job));
  });

  app.delete('/api/jobs/:id', (req, res) => {
    if (!jobs) { res.status(503).json({ error: 'jobs not available' }); return; }
    const removed = jobs.remove(req.params.id);
    if (removed) audit?.record({ role: roleOf(req), action: 'job.delete', target: req.params.id, ok: true });
    res.json({ removed });
  });

  // Run a job immediately, regardless of schedule.
  app.post('/api/jobs/:id/run', async (req, res) => {
    if (!jobs || !scheduler) { res.status(503).json({ error: 'jobs not available' }); return; }
    const job = jobs.get(req.params.id);
    if (!job) { res.status(404).json({ error: 'unknown job' }); return; }
    const result = await scheduler.runOne(job);
    audit?.record({ role: roleOf(req), action: 'job.run', target: job.id, detail: job.command.type, count: result.ok + result.failed, ok: result.failed === 0 });
    res.json({ job: job.id, ...result });
  });

  // ── Provisioning: generate an onboarding QR for a new device's captive portal ─
  app.post('/api/provision', async (req, res) => {
    const body = req.body ?? {};
    const config = buildProvisionConfig(body);
    if (!config) {
      res.status(400).json({ error: 'provide at least a WiFi SSID or an MQTT host' });
      return;
    }
    const apHost = typeof body.apHost === 'string' && body.apHost ? body.apHost : '192.168.4.1';
    const url = provisionUrl(apHost, config);
    const qr = await provisionQrDataUrl(url);
    res.json({ url, qr, config });
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

    // Staged/canary rollout: deploy to the first `canary` devices, and only push
    // to the rest if their command-ack rate clears `minAckRate` (default 100%).
    // This catches an obviously-bad rollout (offline/rejecting devices) before it
    // hits the whole fleet. (Ack = the device accepted the OTA command; true
    // flash-success gating would watch the heartbeat `version` change over time —
    // see the Fleet grid; left as a follow-up needing device telemetry.)
    const canaryN = Math.floor(Number((req.body ?? {}).canary) || 0);
    const minAckRate = typeof (req.body ?? {}).minAckRate === 'number' ? (req.body as { minAckRate: number }).minAckRate : 1;

    if (canaryN > 0 && canaryN < targets.length) {
      const canaryTargets = targets.slice(0, canaryN);
      const canaryResults = await fanOut(bridge, canaryTargets, 'ota_url', { url });
      const canaryOk = canaryResults.filter((r) => r.ok).length;
      const ackRate = canaryOk / canaryTargets.length;
      if (ackRate < minAckRate) {
        audit?.record({ role: roleOf(req), action: 'firmware.deploy', target: name, detail: 'canary-halted', count: canaryTargets.length, ok: false });
        res.json({
          url, staged: true, halted: true,
          reason: `canary ack ${Math.round(ackRate * 100)}% < required ${Math.round(minAckRate * 100)}% — rollout halted`,
          canaryCount: canaryTargets.length, canary: canaryResults,
        });
        return;
      }
      const restResults = await fanOut(bridge, targets.slice(canaryN), 'ota_url', { url });
      const all = [...canaryResults, ...restResults];
      const okAll = all.filter((r) => r.ok).length;
      audit?.record({ role: roleOf(req), action: 'firmware.deploy', target: name, detail: 'staged', count: all.length, ok: okAll === all.length });
      res.json({ url, staged: true, halted: false, canaryCount: canaryTargets.length, count: all.length, ok: okAll, results: all });
      return;
    }

    const results = await fanOut(bridge, targets, 'ota_url', { url });
    const okCount = results.filter((r) => r.ok).length;
    audit?.record({ role: roleOf(req), action: 'firmware.deploy', target: name, detail: 'ota_url', count: results.length, ok: okCount === results.length });
    res.json({ url, count: results.length, ok: okCount, results });
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

  wss.on('connection', (ws, req) => {
    // The browser can't set an Authorization header on a WebSocket, but the
    // login cookie rides along on the upgrade request — validate it.
    if (!auth.ok(req as any)) { ws.close(1008, 'unauthorized'); return; }
    ws.send(JSON.stringify({ type: 'snapshot', devices: registry.list() }));
    ws.send(JSON.stringify({ type: 'alerts', active: alerts.activeAlerts() }));
  });

  registry.on('change', (device) => broadcast({ type: 'device', device }));
  alerts.on('alert', (alert) => broadcast({ type: 'alert', alert }));

  return server;
}
