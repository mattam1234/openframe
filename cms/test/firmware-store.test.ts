import { test } from 'node:test';
import assert from 'node:assert/strict';
import os from 'node:os';
import fs from 'node:fs';
import path from 'node:path';
import { FirmwareStore } from '../src/firmware-store';

test('sanitize strips path traversal and unsafe characters', () => {
  assert.equal(FirmwareStore.sanitize('../../etc/passwd'), 'passwd');
  assert.equal(FirmwareStore.sanitize('a/b/c.bin'), 'c.bin');
  assert.equal(FirmwareStore.sanitize('weird name!.bin'), 'weird_name_.bin');
  assert.equal(FirmwareStore.sanitize('ok-1.2.3_v.bin'), 'ok-1.2.3_v.bin');
});

test('list / has / remove operate on stored files', () => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'of-fw-'));
  const store = new FirmwareStore(dir);
  assert.deepEqual(store.list(), []);
  fs.writeFileSync(path.join(store.dir, 'fw-0.1.bin'), 'data');
  assert.equal(store.has('fw-0.1.bin'), true);
  assert.equal(store.has('nope.bin'), false);
  assert.equal(store.list().length, 1);
  assert.equal(store.list()[0].name, 'fw-0.1.bin');
  assert.equal(store.remove('fw-0.1.bin'), true);
  assert.equal(store.remove('fw-0.1.bin'), false);
  assert.equal(store.list().length, 0);
});

test('has() resists path traversal', () => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'of-fw-'));
  const store = new FirmwareStore(dir);
  assert.equal(store.has('../../../etc/hosts'), false);
});
