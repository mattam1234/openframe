import { test } from 'node:test';
import assert from 'node:assert/strict';
import { DeviceRegistry } from '../src/registry';
import { Device } from '../src/types';

test('applyStatus marks online and merges heartbeat fields', () => {
  const r = new DeviceRegistry(1000);
  r.applyStatus('a', { name: 'Frame', board: 'ESP32', freeHeap: 50000 });
  const d = r.get('a')!;
  assert.equal(d.online, true);
  assert.equal(d.name, 'Frame');
  assert.equal(d.board, 'ESP32');
  assert.notEqual(d.lastSeen, null);
});

test('LWT offline flips online to false; rebirth via heartbeat flips back', () => {
  const r = new DeviceRegistry(1000);
  r.applyStatus('a', { name: 'X' });
  r.applyPresence('a', 'offline');
  assert.equal(r.get('a')!.online, false);
  r.applyStatus('a', { name: 'X' });
  assert.equal(r.get('a')!.online, true);
});

test('sweep expires a stale heartbeat regardless of last presence value', () => {
  const r = new DeviceRegistry(100);
  let changes = 0;
  r.on('change', () => changes++);
  r.applyStatus('a', {});
  r.applyPresence('a', 'offline');
  r.applyStatus('a', {}); // online again, lastPresence still 'offline'
  r.get('a')!.lastSeen = Date.now() - 5000; // make stale
  r.sweep();
  assert.equal(r.get('a')!.online, false, 'stale device must be expired');
  const before = changes;
  r.sweep(); // idempotent — no further change events
  assert.equal(changes, before, 'second sweep emits nothing');
});

test('sweep leaves a birth-only device (no heartbeat) to the LWT', () => {
  const r = new DeviceRegistry(100);
  r.applyPresence('xyz', 'online'); // retained birth, no heartbeat -> lastSeen null
  r.sweep();
  assert.equal(r.get('xyz')!.online, true);
});

test('setTags dedupes/trims; unknown device returns false', () => {
  const r = new DeviceRegistry(1000);
  assert.equal(r.setTags('missing', ['x']), false);
  r.applyStatus('a', {});
  assert.equal(r.setTags('a', [' lobby ', 'lobby', 'floor1', '']), true);
  assert.deepEqual(r.get('a')!.tags, ['lobby', 'floor1']);
});

test('loadSnapshot restores devices offline and normalizes tags', () => {
  const r = new DeviceRegistry(1000);
  const saved = [{ deviceId: 'a', online: true, lastSeen: 1, lastPresence: 'online', presenceAt: 1, updatedAt: 1 }] as unknown as Device[];
  r.loadSnapshot(saved);
  const d = r.get('a')!;
  assert.equal(d.online, false, 'never trust persisted liveness');
  assert.deepEqual(d.tags, []);
});
