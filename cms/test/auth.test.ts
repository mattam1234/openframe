import { describe, it, before, after, test } from 'node:test';
import assert from 'node:assert/strict';
import os from 'node:os';
import fs from 'node:fs';
import path from 'node:path';
import http from 'node:http';
import { tokenMatches } from '../src/auth';
import { DeviceRegistry } from '../src/registry';
import { MqttBridge } from '../src/mqtt';
import { createServer } from '../src/server';
import { TemplateStore } from '../src/templates';
import { HistoryStore } from '../src/history';
import { AlertManager } from '../src/alerts';
import { FirmwareStore } from '../src/firmware-store';
import { JsonStore } from '../src/store';

test('tokenMatches is correct and length-safe', () => {
  assert.equal(tokenMatches('secret', 'secret'), true);
  assert.equal(tokenMatches('secret', 'secrxt'), false);
  assert.equal(tokenMatches('short', 'longertoken'), false);
  assert.equal(tokenMatches('', ''), true);
});

describe('CMS auth (token configured)', () => {
  const TOKEN = 'super-secret-token';
  const PORT = 4155;
  let server: http.Server;
  let bridge: MqttBridge;

  const api = (p: string, init?: RequestInit) => fetch(`http://127.0.0.1:${PORT}${p}`, init);

  before(async () => {
    const dataDir = fs.mkdtempSync(path.join(os.tmpdir(), 'of-auth-'));
    const cfg = { mqttUrl: 'mqtt://127.0.0.1:1', baseTopic: 'openframe', offlineTimeoutMs: 90000, commandTimeoutMs: 500, port: PORT, dataDir } as any;
    const registry = new DeviceRegistry(cfg.offlineTimeoutMs);
    bridge = new MqttBridge(cfg, registry); // no broker — just must not crash
    const templates = new TemplateStore(new JsonStore(dataDir, 't.json'));
    const history = new HistoryStore(new JsonStore(dataDir, 'h.json'), 720);
    const alerts = new AlertManager({ lowHeapBytes: 1, lowRssiDbm: -200 });
    const firmware = new FirmwareStore(dataDir);
    server = createServer(registry, bridge, templates, history, alerts, firmware, 'http://x', TOKEN);
    await new Promise<void>((r) => server.listen(PORT, r));
  });

  after(async () => {
    try { bridge.close(); } catch {}
    await new Promise<void>((r) => server.close(() => r()));
  });

  it('GET /api/health is public', async () => {
    assert.equal((await api('/api/health')).status, 200);
  });

  it('GET /api/auth reports authRequired and unauthed', async () => {
    const body = await (await api('/api/auth')).json() as any;
    assert.equal(body.authRequired, true);
    assert.equal(body.authed, false);
  });

  it('protected endpoint is 401 without a token', async () => {
    assert.equal((await api('/api/devices')).status, 401);
  });

  it('Bearer token grants access', async () => {
    const res = await api('/api/devices', { headers: { Authorization: `Bearer ${TOKEN}` } });
    assert.equal(res.status, 200);
  });

  it('wrong Bearer token is rejected', async () => {
    const res = await api('/api/devices', { headers: { Authorization: 'Bearer nope' } });
    assert.equal(res.status, 401);
  });

  it('login with correct token sets a cookie that authorizes', async () => {
    const login = await api('/api/login', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ token: TOKEN }) });
    assert.equal(login.status, 200);
    const cookie = login.headers.get('set-cookie')!;
    assert.match(cookie, /of_token=/);
    assert.match(cookie, /HttpOnly/i);
    const withCookie = await api('/api/devices', { headers: { Cookie: cookie.split(';')[0] } });
    assert.equal(withCookie.status, 200);
  });

  it('login with wrong token is 401 and sets no cookie', async () => {
    const res = await api('/api/login', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ token: 'wrong' }) });
    assert.equal(res.status, 401);
    assert.equal(res.headers.get('set-cookie'), null);
  });

  it('repeated login attempts are rate-limited (429)', async () => {
    let got429 = false;
    for (let i = 0; i < 15; i++) {
      const res = await api('/api/login', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ token: 'wrong' }) });
      if (res.status === 429) { got429 = true; break; }
    }
    assert.equal(got429, true, 'brute-force attempts must eventually be throttled');
  });
});

describe('CMS auth (disabled by default)', () => {
  const PORT = 4156;
  let server: http.Server;
  let bridge: MqttBridge;
  before(async () => {
    const dataDir = fs.mkdtempSync(path.join(os.tmpdir(), 'of-auth-'));
    const cfg = { mqttUrl: 'mqtt://127.0.0.1:1', baseTopic: 'openframe', offlineTimeoutMs: 90000, commandTimeoutMs: 500, port: PORT, dataDir } as any;
    const registry = new DeviceRegistry(cfg.offlineTimeoutMs);
    bridge = new MqttBridge(cfg, registry);
    server = createServer(
      registry, bridge,
      new TemplateStore(new JsonStore(dataDir, 't.json')),
      new HistoryStore(new JsonStore(dataDir, 'h.json'), 720),
      new AlertManager({ lowHeapBytes: 1, lowRssiDbm: -200 }),
      new FirmwareStore(dataDir),
      'http://x', undefined, // no token
    );
    await new Promise<void>((r) => server.listen(PORT, r));
  });
  after(async () => {
    try { bridge.close(); } catch {}
    await new Promise<void>((r) => server.close(() => r()));
  });
  it('protected endpoints are open when no token is configured', async () => {
    assert.equal((await fetch(`http://127.0.0.1:${PORT}/api/devices`)).status, 200);
    const auth = await (await fetch(`http://127.0.0.1:${PORT}/api/auth`)).json() as any;
    assert.equal(auth.authRequired, false);
  });
});
