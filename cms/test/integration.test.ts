import { describe, it, before, after } from 'node:test';
import assert from 'node:assert/strict';
import os from 'node:os';
import fs from 'node:fs';
import path from 'node:path';
import http from 'node:http';
import { createServer as createBrokerServer } from 'aedes-server-factory';
import { Aedes } from 'aedes';
import mqtt, { MqttClient } from 'mqtt';
import { DeviceRegistry } from '../src/registry';
import { MqttBridge } from '../src/mqtt';
import { createServer } from '../src/server';
import { TemplateStore } from '../src/templates';
import { HistoryStore } from '../src/history';
import { AlertManager } from '../src/alerts';
import { FirmwareStore } from '../src/firmware-store';
import { JsonStore } from '../src/store';

const BROKER_PORT = 1902;
const PORT = 4150;
const NOURL_PORT = 4151;
const DEV = 'abcd1234ef56';
const base = `openframe/${DEV}`;
const PUBLIC_URL = 'http://10.0.0.9:4150';

const sleep = (ms: number) => new Promise((r) => setTimeout(r, ms));
const api = (p: string) => `http://127.0.0.1:${PORT}${p}`;
const getJson = async (p: string) => (await fetch(api(p))).json() as any;
const postJson = async (p: string, body: unknown) =>
  fetch(api(p), { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body) });

describe('CMS integration', () => {
  let aedes: any;
  let brokerServer: any;
  let bridge: MqttBridge;
  let server: http.Server;
  let serverNoUrl: http.Server;
  let firmware: FirmwareStore;
  let dev: MqttClient;
  let lastCmd: any = null;

  before(async () => {
    const dataDir = fs.mkdtempSync(path.join(os.tmpdir(), 'of-cms-'));
    const cfg = {
      mqttUrl: `mqtt://127.0.0.1:${BROKER_PORT}`, baseTopic: 'openframe',
      offlineTimeoutMs: 90000, commandTimeoutMs: 2000, port: PORT, dataDir,
    } as any;

    aedes = await Aedes.createBroker();
    brokerServer = createBrokerServer(aedes);
    await new Promise<void>((r) => brokerServer.listen(BROKER_PORT, '127.0.0.1', r));

    const registry = new DeviceRegistry(cfg.offlineTimeoutMs);
    bridge = new MqttBridge(cfg, registry);
    const templates = new TemplateStore(new JsonStore(dataDir, 't.json'));
    const history = new HistoryStore(new JsonStore(dataDir, 'h.json'), 720);
    const alerts = new AlertManager({ lowHeapBytes: 20000, lowRssiDbm: -85 });
    firmware = new FirmwareStore(dataDir);
    registry.on('change', (d) => { if (history.record(d)) history.persist(); alerts.evaluate(d); });

    server = createServer(registry, bridge, templates, history, alerts, firmware, PUBLIC_URL);
    await new Promise<void>((r) => server.listen(PORT, r));
    // A second server sharing the stores but with NO public URL (OTA must refuse).
    serverNoUrl = createServer(registry, bridge, templates, history, alerts, firmware, undefined);
    await new Promise<void>((r) => serverNoUrl.listen(NOURL_PORT, r));
    await sleep(500); // let the bridge connect + subscribe

    dev = mqtt.connect(`mqtt://127.0.0.1:${BROKER_PORT}`);
    await new Promise<void>((r) => dev.on('connect', () => r()));
    await new Promise<void>((r) => dev.subscribe(`${base}/cmd`, { qos: 1 }, () => r()));
    dev.on('message', (t, p) => {
      if (t !== `${base}/cmd`) return;
      lastCmd = JSON.parse(p.toString());
      const reply: any = { id: lastCmd.id, type: lastCmd.type, ok: true };
      if (lastCmd.type === 'get_profiles') { reply.active = 'home'; reply.profiles = [{ id: 'home', name: 'Home' }]; }
      if (lastCmd.type === 'get_screens') {
        reply.screens = [{
          id: 'oled', type: 'ssd1306', width: 128, height: 64, page: 'main', title: 'Main',
          widgets: [{ x: 0, y: 0, size: 2, text: 'Lobby' }, { x: 0, y: 20, size: 1, text: '21.4°C' }],
        }];
      }
      dev.publish(`${base}/cmd/result`, JSON.stringify(reply), { qos: 1 });
    });
    dev.publish(`${base}/online`, 'online', { retain: true, qos: 1 });
    dev.publish(`${base}/status`, JSON.stringify({ deviceId: DEV, name: 'Frame', board: 'ESP32', freeHeap: 50000 }), { retain: true, qos: 1 });
    await sleep(500);
  });

  after(async () => {
    try { dev?.end(true); } catch {}
    try { bridge?.close(); } catch {}
    await new Promise<void>((r) => server?.close(() => r()));
    await new Promise<void>((r) => serverNoUrl?.close(() => r()));
    await new Promise<void>((r) => brokerServer?.close(() => r()));
    aedes?.close?.();
  });

  it('ingests presence + heartbeat', async () => {
    const { devices } = await getJson('/api/devices');
    const d = devices.find((x: any) => x.deviceId === DEV);
    assert.ok(d, 'device ingested');
    assert.equal(d.online, true);
    assert.equal(d.board, 'ESP32');
  });

  it('single command round-trip resolves with the device ack', async () => {
    const res = await postJson(`/api/devices/${DEV}/cmd`, { type: 'identify' });
    const body = await res.json() as any;
    assert.equal(res.status, 200);
    assert.equal(body.ok, true);
  });

  it('get_profiles relays the device profile list', async () => {
    const res = await postJson(`/api/devices/${DEV}/cmd`, { type: 'get_profiles' });
    const body = await res.json() as any;
    assert.equal(body.active, 'home');
    assert.equal(body.profiles[0].id, 'home');
  });

  it('get_screens relays the device live screen state', async () => {
    const body = await getJson(`/api/devices/${DEV}/screens`);
    assert.equal(body.ok, true);
    assert.equal(body.screens.length, 1);
    assert.equal(body.screens[0].id, 'oled');
    assert.equal(body.screens[0].width, 128);
    assert.equal(body.screens[0].widgets[0].text, 'Lobby');
  });

  it('bulk command targets the whole fleet', async () => {
    const res = await postJson('/api/commands', { type: 'identify' });
    const body = await res.json() as any;
    assert.equal(body.count, 1);
    assert.equal(body.ok, 1);
  });

  it('tags + tag-targeting', async () => {
    const t = await fetch(api(`/api/devices/${DEV}/tags`), {
      method: 'PUT', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ tags: ['lobby', 'lobby'] }),
    });
    assert.equal(t.status, 200);
    const hit = await (await postJson('/api/commands', { type: 'identify', tags: ['lobby'] })).json() as any;
    assert.equal(hit.ok, 1);
    const miss = await postJson('/api/commands', { type: 'identify', tags: ['nope'] });
    assert.equal(miss.status, 400, 'no devices match -> 400');
  });

  it('exposes Prometheus /metrics with fleet + per-device gauges', async () => {
    const res = await fetch(api('/metrics'));
    assert.equal(res.status, 200);
    assert.match(res.headers.get('content-type') || '', /text\/plain/);
    const body = await res.text();
    assert.match(body, /openframe_cms_devices_total \d+/);
    assert.match(body, /# TYPE openframe_cms_devices_online gauge/);
    // The ingested device should appear as a labelled per-device series.
    assert.ok(body.includes(`openframe_device_online{device="${DEV}"}`), 'per-device gauge present');
  });

  it('template create + deploy carries the payload to the device', async () => {
    const created = await (await postJson('/api/templates', { name: 'cfg', command: { type: 'apply_config', payload: { device: { name: 'Lobby' } } } })).json() as any;
    const dep = await (await postJson(`/api/templates/${created.id}/deploy`, { deviceIds: [DEV] })).json() as any;
    assert.equal(dep.ok, 1);
    await sleep(100);
    assert.equal(lastCmd.type, 'apply_config');
    assert.deepEqual(lastCmd.payload, { device: { name: 'Lobby' } });
  });

  it('firmware upload → serve → deploy uses the configured URL', async () => {
    const bytes = Buffer.from('FW-CONTENT');
    const fd = new FormData();
    fd.append('file', new Blob([bytes]), 'fw-0.2.0.bin');
    const up = await (await fetch(api('/api/firmware'), { method: 'POST', body: fd })).json() as any;
    assert.equal(up.name, 'fw-0.2.0.bin');

    const dl = Buffer.from(await (await fetch(api('/firmware/fw-0.2.0.bin'))).arrayBuffer());
    assert.ok(dl.equals(bytes), 'served bytes match');

    const dep = await (await postJson('/api/firmware/fw-0.2.0.bin/deploy', { deviceIds: [DEV] })).json() as any;
    await sleep(100);
    const expected = `${PUBLIC_URL}/firmware/fw-0.2.0.bin`;
    assert.equal(dep.url, expected);
    assert.equal(lastCmd.type, 'ota_url');
    assert.equal(lastCmd.payload.url, expected);
  });

  it('provision endpoint returns a QR data URL and a decodable link', async () => {
    const res = await postJson('/api/provision', { ssid: 'Net', password: 'pw', mqttHost: 'broker' });
    const body = await res.json() as any;
    assert.equal(res.status, 200);
    assert.match(body.qr, /^data:image\/png;base64,/);
    assert.match(body.url, /\/\?provision=[A-Za-z0-9_-]+#\/settings$/);
    assert.deepEqual(body.config.wifi.networks, [{ ssid: 'Net', password: 'pw' }]);

    const empty = await postJson('/api/provision', {});
    assert.equal(empty.status, 400);
  });

  it('SECURITY: firmware deploy is refused (500) without CMS_PUBLIC_URL and sends no command', async () => {
    lastCmd = null;
    const res = await fetch(`http://127.0.0.1:${NOURL_PORT}/api/firmware/fw-0.2.0.bin/deploy`, {
      method: 'POST', headers: { 'Content-Type': 'application/json', Host: 'evil.example.com' }, body: JSON.stringify({ deviceIds: [DEV] }),
    });
    assert.equal(res.status, 500);
    await sleep(100);
    assert.equal(lastCmd, null, 'no ota_url command must be sent');
  });
});
