import { describe, it } from 'node:test';
import assert from 'node:assert/strict';
import os from 'node:os';
import fs from 'node:fs';
import path from 'node:path';
import { AuditLog, AuditEntry } from '../src/audit';
import { JsonStore } from '../src/store';

describe('AuditLog', () => {
  const mkStore = () => {
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'of-audit-'));
    return new JsonStore<{ entries: AuditEntry[] }>(dir, 'audit.json');
  };

  it('records newest-first', () => {
    const log = new AuditLog(mkStore());
    log.record({ role: 'admin', action: 'device.cmd', target: 'a', detail: 'reboot', ok: true });
    log.record({ role: 'admin', action: 'commands.bulk', detail: 'identify', count: 3, ok: true });
    const list = log.list();
    assert.equal(list.length, 2);
    assert.equal(list[0].action, 'commands.bulk');
    assert.ok(typeof list[0].t === 'number');
  });

  it('caps at max entries', () => {
    const log = new AuditLog(mkStore(), 5);
    for (let i = 0; i < 20; i++) log.record({ role: 'open', action: 'device.cmd', detail: String(i) });
    assert.equal(log.list().length, 5);
    // Newest first → the last recorded ('19') is on top.
    assert.equal(log.list()[0].detail, '19');
  });

  it('persists across instances', () => {
    const store = mkStore();
    new AuditLog(store).record({ role: 'admin', action: 'firmware.deploy', target: 'fw.bin', ok: true });
    const reopened = new AuditLog(store);
    assert.equal(reopened.list().length, 1);
    assert.equal(reopened.list()[0].action, 'firmware.deploy');
  });
});
