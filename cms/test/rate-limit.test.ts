import { test } from 'node:test';
import assert from 'node:assert/strict';
import { RateLimiter } from '../src/rate-limit';

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
