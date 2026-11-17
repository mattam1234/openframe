import { test } from 'node:test';
import assert from 'node:assert/strict';
import { RateLimiter, FailureLockout } from '../src/rate-limit';

test('allows up to max within the window, then blocks', () => {
  const rl = new RateLimiter(3, 1000);
  assert.equal(rl.allow('ip', 0), true);
  assert.equal(rl.allow('ip', 10), true);
  assert.equal(rl.allow('ip', 20), true);
  assert.equal(rl.allow('ip', 30), false, '4th in window blocked');
});

test('window slides — old hits expire', () => {
  const rl = new RateLimiter(2, 1000);
  assert.equal(rl.allow('ip', 0), true);
  assert.equal(rl.allow('ip', 500), true);
  assert.equal(rl.allow('ip', 600), false);
  assert.equal(rl.allow('ip', 1600), true, 'first hit (t=0) has aged out');
});

test('keys are independent', () => {
  const rl = new RateLimiter(1, 1000);
  assert.equal(rl.allow('a', 0), true);
  assert.equal(rl.allow('b', 0), true);
  assert.equal(rl.allow('a', 0), false);
});

test('FailureLockout locks after maxFails and expires after lockMs', () => {
  const lo = new FailureLockout(3, 1000);
  assert.equal(lo.locked('ip', 0), false);
  assert.equal(lo.recordFailure('ip', 0), 0, '1st fail: not locked');
  assert.equal(lo.recordFailure('ip', 10), 0, '2nd fail: not locked');
  assert.ok(lo.recordFailure('ip', 20) > 0, '3rd fail: locked');
  assert.equal(lo.locked('ip', 100), true, 'still locked within window');
  assert.equal(lo.locked('ip', 1100), false, 'lock expired after lockMs');
});

test('FailureLockout reset clears failures on success', () => {
  const lo = new FailureLockout(2, 1000);
  lo.recordFailure('ip', 0);
  lo.reset('ip');
  assert.equal(lo.recordFailure('ip', 10), 0, 'counter reset — single fail not locked');
  assert.equal(lo.locked('ip', 20), false);
});

test('FailureLockout evicts idle sub-threshold entries once the map is large', () => {
  const lockMs = 1000;
  const idleMs = 10_000;
  const lo = new FailureLockout(3, lockMs, idleMs);

  // t=0: 999 distinct clients fail once each and never come back. Later, one
  // client gets to one failure short of a lockout.
  for (let i = 0; i < 999; i++) lo.recordFailure(`drive-by-${i}`, 0);
  lo.recordFailure('near', idleMs - 2);
  lo.recordFailure('near', idleMs - 1);
  assert.equal(lo.size, 1000, 'nothing is evicted below the size threshold');

  // The next new key pushes the map over the threshold and triggers a prune:
  // every drive-by entry (idle >= idleMs) goes, the recent one stays.
  const t = idleMs + 5;
  for (let i = 0; i < 3; i++) lo.recordFailure('locked', t);
  assert.equal(lo.size, 2, 'only the recently-failing and the locked key remain');
  assert.equal(lo.locked('locked', t), true);

  // A burst of fresh clients forces another prune while 'locked' is locked:
  // neither it nor the fresh, un-idle entries may be dropped.
  for (let i = 0; i < 1000; i++) lo.recordFailure(`burst-${i}`, t + 1);
  assert.equal(lo.size, 1002, 'fresh entries and the locked key all survive a prune');
  assert.equal(lo.locked('locked', t + lockMs - 1), true, 'lock unaffected by eviction');

  // The recently-failing key kept its count: one more failure locks it.
  assert.ok(lo.recordFailure('near', t + 2) > 0, 'sub-threshold count survived eviction');
});

test('FailureLockout idle period is never shorter than the lock', () => {
  const lo = new FailureLockout(3, 5000, 1); // asks for a 1 ms idle period
  lo.recordFailure('a', 0);
  lo.recordFailure('a', 0);
  for (let i = 0; i < 1000; i++) lo.recordFailure(`x-${i}`, 4000); // forces a prune at t=4000
  assert.ok(lo.recordFailure('a', 4001) > 0, 'count from t=0 kept: idle is clamped up to lockMs');
});

test('FailureLockout drops expired locks nobody asked about again', () => {
  const lo = new FailureLockout(1, 1000);
  for (let i = 0; i < 1000; i++) lo.recordFailure(`gone-${i}`, 0); // all locked until t=1000
  assert.equal(lo.size, 1000);
  lo.recordFailure('later', 2000);
  assert.equal(lo.size, 1, 'expired locks pruned; the fresh lock stays');
  assert.equal(lo.locked('later', 2001), true);
});
