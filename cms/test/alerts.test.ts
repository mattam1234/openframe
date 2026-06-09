import { test } from 'node:test';
import assert from 'node:assert/strict';
import { AlertManager } from '../src/alerts';
import { Device } from '../src/types';

function device(over: Partial<Device>): Device {
  return {
    deviceId: 'a', tags: [], online: true, lastSeen: Date.now(),
    lastPresence: 'online', presenceAt: Date.now(), updatedAt: Date.now(), ...over,
  };
}

test('offline alert raises when offline and resolves when back online', () => {
  const m = new AlertManager({ lowHeapBytes: 20000, lowRssiDbm: -85 });
  m.evaluate(device({ online: false }));
  assert.ok(m.activeAlerts().some((a) => a.type === 'offline'));
  m.evaluate(device({ online: true, freeHeap: 50000, rssi: -50 }));
  assert.equal(m.activeAlerts().some((a) => a.type === 'offline'), false);
});

test('low_heap raises below threshold and resolves on recovery', () => {
  const m = new AlertManager({ lowHeapBytes: 20000, lowRssiDbm: -85 });
  m.evaluate(device({ freeHeap: 15000 }));
  assert.ok(m.activeAlerts().some((a) => a.type === 'low_heap'));
  m.evaluate(device({ freeHeap: 40000 }));
  assert.equal(m.activeAlerts().some((a) => a.type === 'low_heap'), false);
});

test('low_rssi ignores rssi==0 (disconnected) and offline devices', () => {
  const m = new AlertManager({ lowHeapBytes: 20000, lowRssiDbm: -85 });
  m.evaluate(device({ rssi: 0 }));
  assert.equal(m.activeAlerts().some((a) => a.type === 'low_rssi'), false);
  m.evaluate(device({ online: false, rssi: -90 }));
  assert.equal(m.activeAlerts().some((a) => a.type === 'low_rssi'), false);
  m.evaluate(device({ rssi: -90 }));
  assert.ok(m.activeAlerts().some((a) => a.type === 'low_rssi'));
});

test('resolved alerts land in recent history and emit events', () => {
  const m = new AlertManager({ lowHeapBytes: 20000, lowRssiDbm: -85 });
  const events: string[] = [];
  m.on('alert', (a) => events.push(`${a.type}:${a.resolvedAt ? 'resolved' : 'raised'}`));
  m.evaluate(device({ freeHeap: 1000 }));
  m.evaluate(device({ freeHeap: 50000 }));
  assert.deepEqual(events, ['low_heap:raised', 'low_heap:resolved']);
  assert.ok(m.recentAlerts().some((a) => a.type === 'low_heap' && a.resolvedAt));
});
