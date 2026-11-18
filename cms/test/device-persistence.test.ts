import { test } from 'node:test';
import assert from 'node:assert/strict';
import os from 'node:os';
import fs from 'node:fs';
import path from 'node:path';
import { DeviceRegistry } from '../src/registry';
import { JsonStore } from '../src/store';
import { persistDevices } from '../src/device-persistence';
import { Device } from '../src/types';

function fresh() {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'of-devpersist-'));
  const file = path.join(dir, 'devices.json');
  const store = new JsonStore<{ devices: Device[] }>(dir, 'devices.json');
  const registry = new DeviceRegistry(90_000);
  // A long heartbeat debounce: anything on disk during the test got there
  // through the immediate operator-edit path, not the timer.
  persistDevices(registry, store, 60_000);
  const onDisk = (): Device | undefined =>
    (JSON.parse(fs.readFileSync(file, 'utf8')) as { devices: Device[] }).devices.find((d) => d.deviceId === 'a');
  return { file, store, registry, onDisk };
}

test('heartbeats and presence only schedule a debounced write', () => {
  const { file, store, registry } = fresh();
  registry.applyStatus('a', { name: 'Frame' });
  registry.applyPresence('a', 'online');
  registry.applyStatus('a', { name: 'Frame' });
  assert.equal(fs.existsSync(file), false, 'no write per heartbeat');
  store.flush(); // what the debounce timer / shutdown would do
  assert.equal(fs.existsSync(file), true);
});

test('tags, notes and site edits are on disk as soon as the call returns', () => {
  const { file, registry, onDisk } = fresh();
  registry.applyStatus('a', { name: 'Frame' });
  assert.equal(fs.existsSync(file), false);

  assert.equal(registry.setTags('a', ['lobby']), true);
  assert.deepEqual(onDisk()?.tags, ['lobby']);

  assert.equal(registry.setNotes('a', 'north wall'), true);
  assert.equal(onDisk()?.notes, 'north wall');

  assert.equal(registry.setSite('a', 'HQ'), true);
  assert.equal(onDisk()?.site, 'HQ');
  // Earlier edits are still there: each write is the full snapshot.
  assert.deepEqual(onDisk()?.tags, ['lobby']);
});

test('an edit does not turn later heartbeats into immediate writes', () => {
  const { file, store, registry, onDisk } = fresh();
  registry.applyStatus('a', { name: 'Frame' });
  registry.setNotes('a', 'north wall');
  const afterEdit = fs.readFileSync(file, 'utf8');

  for (let i = 0; i < 20; i++) registry.applyStatus('a', { name: 'Frame', freeHeap: 40_000 + i });
  assert.equal(fs.readFileSync(file, 'utf8'), afterEdit, 'heartbeats after an edit stay debounced');

  store.flush();
  assert.equal(onDisk()?.freeHeap, 40_019);
  assert.equal(onDisk()?.notes, 'north wall');
});

test('an edit to an unknown device writes nothing', () => {
  const { file, registry } = fresh();
  assert.equal(registry.setTags('nope', ['x']), false);
  assert.equal(fs.existsSync(file), false);
});
