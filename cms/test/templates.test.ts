import { test } from 'node:test';
import assert from 'node:assert/strict';
import os from 'node:os';
import fs from 'node:fs';
import path from 'node:path';
import { TemplateStore } from '../src/templates';
import { JsonStore } from '../src/store';

function freshStore() {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'of-tpl-'));
  return new TemplateStore(new JsonStore(dir, 'templates.json'));
}

test('upsert requires name and command.type', () => {
  const s = freshStore();
  assert.equal(s.upsert({ name: '', command: { type: 'identify' } }), null);
  assert.equal(s.upsert({ name: 'x', command: { type: '' } }), null);
  assert.equal(s.upsert({ name: 'x' }), null);
});

test('upsert creates then updates the same id (no duplicate)', () => {
  const s = freshStore();
  const created = s.upsert({ name: 'A', command: { type: 'identify' } })!;
  assert.ok(created.id);
  const updated = s.upsert({ id: created.id, name: 'A2', command: { type: 'reboot' } })!;
  assert.equal(updated.id, created.id);
  assert.equal(updated.name, 'A2');
  assert.equal(updated.command.type, 'reboot');
  assert.equal(s.list().length, 1);
});

test('remove deletes', () => {
  const s = freshStore();
  const t = s.upsert({ name: 'A', command: { type: 'identify' } })!;
  assert.equal(s.remove(t.id), true);
  assert.equal(s.remove(t.id), false);
  assert.equal(s.list().length, 0);
});

test('templates persist across store instances', () => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'of-tpl-'));
  const s1 = new TemplateStore(new JsonStore(dir, 'templates.json'));
  s1.upsert({ name: 'Persisted', command: { type: 'reboot' } });
  const s2 = new TemplateStore(new JsonStore(dir, 'templates.json'));
  assert.equal(s2.list().length, 1);
  assert.equal(s2.list()[0].name, 'Persisted');
});
