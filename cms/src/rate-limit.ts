// Tiny in-memory sliding-window rate limiter. Used to throttle login attempts so
// a configured token can't be brute-forced. Keyed by client (IP).
export class RateLimiter {
  private readonly hits = new Map<string, number[]>();

  constructor(private readonly max: number, private readonly windowMs: number) {}

  // Returns true if the call is allowed (and records it), false if over the limit.
  allow(key: string, now: number = Date.now()): boolean {
    const recent = (this.hits.get(key) ?? []).filter((t) => now - t < this.windowMs);
    if (recent.length >= this.max) {
      this.hits.set(key, recent);
      return false;
    }
    recent.push(now);
    this.hits.set(key, recent);
    return true;
  }
}

// Failure-based lockout: after `maxFails` consecutive failures a key is locked out
// for `lockMs`, regardless of rate. Resets on success. Complements RateLimiter
// (which throttles overall attempt rate) to make brute force impractical (#80).
export class FailureLockout {
  private readonly fails = new Map<string, { count: number; until: number }>();

  constructor(private readonly maxFails: number, private readonly lockMs: number) {}

  // True if the key is currently locked out.
  locked(key: string, now: number = Date.now()): boolean {
    const e = this.fails.get(key);
    if (!e) return false;
    if (e.until && e.until <= now) { this.fails.delete(key); return false; }  // lock expired
    return e.until > now;
  }

  // Record a failed attempt. Returns the remaining lock duration in ms (0 if not locked).
  recordFailure(key: string, now: number = Date.now()): number {
    const e = this.fails.get(key) ?? { count: 0, until: 0 };
    e.count += 1;
    if (e.count >= this.maxFails) e.until = now + this.lockMs;
    this.fails.set(key, e);
    return e.until > now ? e.until - now : 0;
  }

  // Clear a key's failure record (call on a successful login).
  reset(key: string): void {
    this.fails.delete(key);
  }
}
