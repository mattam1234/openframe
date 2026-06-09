import { test } from 'node:test';
import assert from 'node:assert/strict';
import os from 'node:os';
import fs from 'node:fs';
import path from 'node:path';
import { HistoryStore } from '../src/history';
import { JsonStore } from '../src/store';
import { Device } from '../src/types';

function freshStore(max: number) {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'of-hist-'));
  return new HistoryStore(new JsonStore(dir, 'history.json'), max);
}
function device(over: Partial<Device>): Device {
  return {
    deviceId: 'a', tags: [], online: true, lastSeen: 1000,
    lastPresence: 'online', presenceAt: 1000, updatedAt: 1000, ...over,
  };
}

test('records a sample only when lastSeen advances', () => {
  const h = freshStore(720);
  assert.equal(h.record(device({ lastSeen: 1000, freeHeap: 100 })), true);
  assert.equal(h.record(device({ lastSeen: 1000, freeHeap: 200 })), false, 'same lastSeen -> no duplicate');
  assert.equal(h.record(device({ lastSeen: 2000, freeHeap: 200 })), true);
  assert.equal(h.get('a').length, 2);
});

test('does not record offline devices or those without a lastSeen', () => {
  const h = freshStore(720);
  assert.equal(h.record(device({ online: false })), false);
  assert.equal(h.record(device({ lastSeen: null })), false);
});

test('caps the rolling window to maxSamples', () => {
  const h = freshStore(3);
  for (let i = 1; i <= 6; i++) h.record(device({ lastSeen: i * 1000, freeHeap: i }));
  const s = h.get('a');
  assert.equal(s.length, 3);
  assert.deepEqual(s.map((x) => x.freeHeap), [4, 5, 6], 'keeps the most recent');
});
