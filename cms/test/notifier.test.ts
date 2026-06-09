import { test } from 'node:test';
import assert from 'node:assert/strict';
import http from 'node:http';
import { notifyAlert } from '../src/notifier';
import { Alert } from '../src/alerts';

const sampleAlert: Alert = {
  id: 'a:offline', deviceId: 'a', type: 'offline', severity: 'critical',
  message: 'Device is offline', raisedAt: 1, resolvedAt: null,
};

test('notifyAlert POSTs the alert as JSON', async () => {
  let received: any = null;
  const server = http.createServer((req, res) => {
    let body = '';
    req.on('data', (c) => (body += c));
    req.on('end', () => {
      received = { method: req.method, ct: req.headers['content-type'], body: JSON.parse(body) };
      res.end('ok');
    });
  });
  await new Promise<void>((r) => server.listen(0, r));
  const port = (server.address() as any).port;

  await notifyAlert(`http://127.0.0.1:${port}/hook`, sampleAlert);
  await new Promise((r) => setTimeout(r, 50));
  await new Promise<void>((r) => server.close(() => r()));

  assert.equal(received.method, 'POST');
  assert.match(received.ct, /application\/json/);
  assert.equal(received.body.type, 'alert');
  assert.equal(received.body.alert.type, 'offline');
  assert.equal(received.body.alert.deviceId, 'a');
});

test('notifyAlert swallows errors from an unreachable host', async () => {
  // Must not throw — a flaky webhook can't be allowed to disrupt the CMS.
  await notifyAlert('http://127.0.0.1:1/nope', sampleAlert);
});
