import { test } from 'node:test';
import assert from 'node:assert/strict';
import { DeviceLogStore, DeviceLogEntry } from '../src/logs';

test('appends structured lines and returns them per device', () => {
  const logs = new DeviceLogStore();
  const e = logs.append('a', { ts: 123, level: 'warning', tag: 'WiFi', msg: 'RSSI low' });
  assert.ok(e);
  logs.append('b', { level: 'error', msg: 'boom' });
  assert.equal(logs.get('a').length, 1);
  assert.equal(logs.get('a')[0].msg, 'RSSI low');
  assert.equal(logs.get('a')[0].tag, 'WiFi');
  assert.equal(logs.get('b')[0].level, 'error');
  assert.equal(logs.get('missing').length, 0);
});

test('rejects malformed lines without throwing', () => {
  const logs = new DeviceLogStore();
  assert.equal(logs.append('a', null), null);
  assert.equal(logs.append('a', 'not an object'), null);
  assert.equal(logs.append('a', {}), null, 'msg is required');
  assert.equal(logs.append('a', { msg: '' }), null, 'empty msg is rejected');
  assert.equal(logs.get('a').length, 0);
});

test('defaults level to warning and tolerates missing optional fields', () => {
  const logs = new DeviceLogStore();
  const e = logs.append('a', { msg: 'hello' });
  assert.equal(e?.level, 'warning');
  assert.equal(e?.tag, undefined);
  assert.ok(typeof e?.t === 'number' && e.t > 0, 'received-at timestamp is stamped');
});

test('caps the rolling window per device', () => {
  const logs = new DeviceLogStore(3);
  for (let i = 1; i <= 6; i++) logs.append('a', { msg: `line ${i}` });
  const lines = logs.get('a');
  assert.equal(lines.length, 3);
  assert.deepEqual(lines.map((l) => l.msg), ['line 4', 'line 5', 'line 6'], 'keeps the most recent');
});

test('emits an entry event for each accepted line', () => {
  const logs = new DeviceLogStore();
  const seen: Array<{ deviceId: string; entry: DeviceLogEntry }> = [];
  logs.on('entry', (deviceId: string, entry: DeviceLogEntry) => seen.push({ deviceId, entry }));
  logs.append('a', { msg: 'one' });
  logs.append('a', 'garbage');
  logs.append('b', { msg: 'two' });
  assert.equal(seen.length, 2, 'rejected lines emit nothing');
  assert.equal(seen[0].deviceId, 'a');
  assert.equal(seen[1].entry.msg, 'two');
});
