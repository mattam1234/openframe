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
    if (this.hits.size > RateLimiter.PRUNE_ABOVE) this.prune(now);
    return true;
  }

  // Keys are only trimmed when they are seen again, so a client that never
  // returns would otherwise stay in the map forever. Drop fully-expired keys
  // once the map gets large.
  private static readonly PRUNE_ABOVE = 1000;

  private prune(now: number): void {
    for (const [key, times] of this.hits) {
      if (!times.some((t) => now - t < this.windowMs)) this.hits.delete(key);
    }
  }
}

// Failure-based lockout: after `maxFails` consecutive failures a key is locked out
// for `lockMs`, regardless of rate. Resets on success. Complements RateLimiter
// (which throttles overall attempt rate) to make brute force impractical (#80).
//
// Memory is bounded by eviction, not by a cap: once the map is large, entries
// that are not locked and have seen no failure for `idleMs` are dropped. idleMs
// is never shorter than lockMs, so forgetting a sub-threshold count gives an
// attacker nothing they don't already have — a full lockout itself only costs
// them lockMs before the counter starts from zero again.
export class FailureLockout {
  private readonly fails = new Map<string, { count: number; until: number; last: number }>();
  private static readonly PRUNE_ABOVE = 1000;
  private static readonly DEFAULT_IDLE_MS = 60 * 60_000;

  private readonly idleMs: number;
  private pruneAbove = FailureLockout.PRUNE_ABOVE;

  constructor(
    private readonly maxFails: number,
    private readonly lockMs: number,
    idleMs: number = FailureLockout.DEFAULT_IDLE_MS,
  ) {
    this.idleMs = Math.max(idleMs, lockMs);
  }

  // Number of keys currently tracked (for tests/diagnostics).
  get size(): number {
    return this.fails.size;
  }

  // True if the key is currently locked out.
  locked(key: string, now: number = Date.now()): boolean {
    const e = this.fails.get(key);
    if (!e) return false;
    if (e.until && e.until <= now) { this.fails.delete(key); return false; }  // lock expired
    return e.until > now;
  }

  // Record a failed attempt. Returns the remaining lock duration in ms (0 if not locked).
  recordFailure(key: string, now: number = Date.now()): number {
    const e = this.fails.get(key) ?? { count: 0, until: 0, last: now };
    e.count += 1;
    e.last = now;
    if (e.count >= this.maxFails) e.until = now + this.lockMs;
    this.fails.set(key, e);
    if (this.fails.size > this.pruneAbove) this.prune(now);
    return e.until > now ? e.until - now : 0;
  }

  // A client that fails once and never returns (or never logs in successfully)
  // would otherwise stay in the map forever. Drop entries whose lock has run
  // out, and unlocked ones idle for idleMs. A currently locked key is never
  // dropped.
  private prune(now: number): void {
    for (const [key, e] of this.fails) {
      if (e.until > now) continue;                                   // locked: keep
      if (e.until !== 0 || now - e.last >= this.idleMs) this.fails.delete(key);
    }
    // If most entries are still live (a burst of distinct clients), don't rescan
    // the whole map on every failure: wait until it has grown again.
    this.pruneAbove = Math.max(FailureLockout.PRUNE_ABOVE, this.fails.size * 2);
  }

  // Clear a key's failure record (call on a successful login).
  reset(key: string): void {
    this.fails.delete(key);
  }
}
