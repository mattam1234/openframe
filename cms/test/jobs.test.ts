import { test } from 'node:test';
import assert from 'node:assert/strict';
import os from 'node:os';
import fs from 'node:fs';
import path from 'node:path';
import { Job, JobStore, JobScheduler, dueJobs, isDue, markRan } from '../src/jobs';
import { JsonStore } from '../src/store';

function freshJobStore() {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'of-jobs-'));
  return new JobStore(new JsonStore(dir, 'jobs.json'));
}

const MIN = 60_000;
const DAY = 86_400_000;

function job(overrides: Partial<Job>): Job {
  return {
    id: 'j', name: 'j', enabled: true,
    schedule: { mode: 'interval', intervalMin: 10 },
    command: { type: 'reboot' },
    target: {},
    ...overrides,
  };
}

test('interval job is due when never run, and after the interval elapses', () => {
  const j = job({ schedule: { mode: 'interval', intervalMin: 10 } });
  assert.equal(isDue(j, 0), true, 'never run → due');
  j.lastRun = 1000;
  assert.equal(isDue(j, 1000 + 9 * MIN), false, 'before interval → not due');
  assert.equal(isDue(j, 1000 + 10 * MIN), true, 'at interval → due');
});

test('daily job is due at/after its minute, once per UTC day', () => {
  // 08:00 UTC = 480 minutes.
  const j = job({ schedule: { mode: 'daily', dailyMinute: 480 } });
  const at0730 = 7 * 60 * MIN + 30 * MIN;
  const at0800 = 8 * 60 * MIN;
  assert.equal(isDue(j, at0730), false, 'before time → not due');
  assert.equal(isDue(j, at0800), true, 'at time → due');
  markRan(j, at0800, { ok: 1, failed: 0 });
  assert.equal(isDue(j, at0800 + MIN), false, 'already ran today → not due again');
  assert.equal(isDue(j, DAY + at0800), true, 'next day → due again');
});

test('disabled jobs are never due', () => {
  const j = job({ enabled: false });
  assert.equal(isDue(j, 10 * DAY), false);
  assert.deepEqual(dueJobs([j], 10 * DAY), []);
});

test('interval job with no intervalMin is inert', () => {
  const j = job({ schedule: { mode: 'interval' } });
  assert.equal(isDue(j, 10 * DAY), false);
});

test('dueJobs filters a mixed set', () => {
  const a = job({ id: 'a', schedule: { mode: 'interval', intervalMin: 5 }, lastRun: 0 });
  const b = job({ id: 'b', enabled: false });
  const c = job({ id: 'c', schedule: { mode: 'daily', dailyMinute: 0 } });
  const due = dueJobs([a, b, c], 6 * MIN).map((j) => j.id);
  assert.deepEqual(due.sort(), ['a', 'c']);
});

test('JobStore upserts, removes, and persists across instances', () => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'of-jobs-'));
  const s1 = new JobStore(new JsonStore(dir, 'jobs.json'));
  s1.upsert(job({ id: 'a', name: 'A' }));
  s1.upsert(job({ id: 'a', name: 'A2' }));          // update, not duplicate
  s1.upsert(job({ id: 'b', name: 'B' }));
  assert.equal(s1.list().length, 2);
  assert.equal(s1.get('a')!.name, 'A2');
  assert.equal(s1.remove('b'), true);
  assert.equal(s1.remove('missing'), false);

  // A new store over the same dir sees the persisted state.
  const s2 = new JobStore(new JsonStore(dir, 'jobs.json'));
  assert.deepEqual(s2.list().map((j) => j.id), ['a']);
});

test('JobScheduler.runOne invokes the runner, stamps the job, and persists', async () => {
  const store = freshJobStore();
  store.upsert(job({ id: 'a', schedule: { mode: 'daily', dailyMinute: 0 } }));
  let calls = 0;
  const sched = new JobScheduler(store, async () => { calls++; return { ok: 3, failed: 1 }; });
  const res = await sched.runOne(store.get('a')!, 8 * 60 * MIN);
  assert.equal(calls, 1);
  assert.deepEqual(res, { ok: 3, failed: 1 });
  const saved = store.get('a')!;
  assert.deepEqual(saved.lastResult, { ok: 3, failed: 1, at: 8 * 60 * MIN });
  assert.ok(saved.lastDailyDay !== undefined);   // daily de-dup stamped
});

test('JobScheduler.runOne records a failure when the runner throws', async () => {
  const store = freshJobStore();
  store.upsert(job({ id: 'a' }));
  const sched = new JobScheduler(store, async () => { throw new Error('boom'); });
  const res = await sched.runOne(store.get('a')!, 1000);
  assert.deepEqual(res, { ok: 0, failed: 1 });
  assert.equal(store.get('a')!.lastResult!.failed, 1);
});

test('JobScheduler.tick runs only due jobs', async () => {
  const store = freshJobStore();
  store.upsert(job({ id: 'due', schedule: { mode: 'interval', intervalMin: 5 } }));      // never run -> due
  store.upsert(job({ id: 'cool', schedule: { mode: 'interval', intervalMin: 60 }, lastRun: 1000 }));
  const ran: string[] = [];
  const sched = new JobScheduler(store, async () => { ran.push('x'); return { ok: 1, failed: 0 }; });
  await sched.tick(2000);   // 'due' is due (never run); 'cool' fired at 1000, 60min not elapsed
  assert.equal(ran.length, 1);
  assert.ok(store.get('due')!.lastRun);
});
