import { test } from 'node:test';
import assert from 'node:assert/strict';
import os from 'node:os';
import fs from 'node:fs';
import path from 'node:path';
import { JsonStore } from '../src/store';

function fresh() {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'of-store-'));
  return { dir, file: path.join(dir, 's.json'), store: new JsonStore<{ n: number }>(dir, 's.json') };
}

test('saveDebounced coalesces a burst into one write of the latest state', async () => {
  const { file, store } = fresh();
  let n = 0;
  let produced = 0;
  for (let i = 0; i < 50; i++) {
    n = i;
    store.saveDebounced(() => { produced++; return { n }; }, 20);
  }
  assert.equal(fs.existsSync(file), false, 'nothing written before the delay');
  await new Promise((r) => setTimeout(r, 80));
  assert.equal(produced, 1);
  assert.deepEqual(store.load(), { n: 49 });
});

test('flush writes a pending debounced save immediately, and only once', () => {
  const { file, store } = fresh();
  let produced = 0;
  store.saveDebounced(() => { produced++; return { n: 7 }; }, 60_000);
  store.flush();
  assert.deepEqual(store.load(), { n: 7 });
  fs.rmSync(file);
  store.flush(); // nothing pending — must not write again
  assert.equal(produced, 1);
  assert.equal(fs.existsSync(file), false);
});

test('debounced saves are compact; saveNow stays human-readable', () => {
  const { file, store } = fresh();
  store.saveDebounced(() => ({ n: 1 }), 60_000);
  store.flush();
  assert.equal(fs.readFileSync(file, 'utf8'), '{"n":1}');
  store.saveNow({ n: 2 });
  assert.match(fs.readFileSync(file, 'utf8'), /\n/);
  assert.deepEqual(store.load(), { n: 2 });
});
