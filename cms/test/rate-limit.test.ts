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
