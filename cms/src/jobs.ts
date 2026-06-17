import { JsonStore } from './store';

// Scheduled fleet jobs (#63): run a fleet command on a schedule — nightly reboot,
// periodic config reconcile, etc. Schedules are kept deliberately simple (no cron
// dependency): a fixed interval, or once-daily at a UTC minute-of-day.

export interface JobSchedule {
  mode: 'interval' | 'daily';
  intervalMin?: number; // mode === 'interval': run every N minutes
  dailyMinute?: number; // mode === 'daily': minutes past 00:00 UTC (0–1439)
}

export interface JobTarget {
  deviceIds?: string[];
  tags?: string[];
  onlineOnly?: boolean;
}

export interface JobCommand {
  type: string;
  payload?: unknown;
}

export interface Job {
  id: string;
  name: string;
  enabled: boolean;
  schedule: JobSchedule;
  command: JobCommand;
  target: JobTarget;
  lastRun?: number; // epoch ms of the last run
  lastResult?: { ok: number; failed: number; at: number };
  lastDailyDay?: number; // UTC day index of the last daily run (de-dups within a day)
}

const MS_PER_MIN = 60_000;
const MS_PER_DAY = 86_400_000;

// Pure: which enabled jobs are due to run at `now` (epoch ms). Used by the
// scheduler each tick and exercised directly in tests.
export function dueJobs(jobs: Job[], now: number): Job[] {
  return jobs.filter((j) => j.enabled && isDue(j, now));
}

export function isDue(job: Job, now: number): boolean {
  if (!job.enabled) return false;
  const s = job.schedule;
  if (s.mode === 'interval') {
    const everyMs = Math.max(1, s.intervalMin ?? 0) * MS_PER_MIN;
    if (!s.intervalMin) return false;
    return job.lastRun == null || now - job.lastRun >= everyMs;
  }
  if (s.mode === 'daily') {
    if (s.dailyMinute == null) return false;
    const day = Math.floor(now / MS_PER_DAY);
    const minuteOfDay = Math.floor((now % MS_PER_DAY) / MS_PER_MIN);
    return minuteOfDay >= s.dailyMinute && job.lastDailyDay !== day;
  }
  return false;
}

// Stamp a job as just-run (mutates the passed object).
export function markRan(job: Job, now: number, result: { ok: number; failed: number }): void {
  job.lastRun = now;
  job.lastResult = { ...result, at: now };
  if (job.schedule.mode === 'daily') job.lastDailyDay = Math.floor(now / MS_PER_DAY);
}

export class JobStore {
  private jobs: Job[] = [];

  constructor(private readonly store: JsonStore<{ jobs: Job[] }>) {
    this.jobs = store.load()?.jobs ?? [];
  }

  list(): Job[] {
    return this.jobs.slice();
  }

  get(id: string): Job | undefined {
    return this.jobs.find((j) => j.id === id);
  }

  // Insert or replace by id. Returns the stored job.
  upsert(job: Job): Job {
    const idx = this.jobs.findIndex((j) => j.id === job.id);
    if (idx >= 0) this.jobs[idx] = job;
    else this.jobs.push(job);
    this.persist();
    return job;
  }

  remove(id: string): boolean {
    const before = this.jobs.length;
    this.jobs = this.jobs.filter((j) => j.id !== id);
    if (this.jobs.length === before) return false;
    this.persist();
    return true;
  }

  persist(): void {
    this.store.saveNow({ jobs: this.jobs });
  }
}

// Runs due jobs on a tick. The actual command dispatch is injected (so the store
// stays free of MQTT/registry deps and the runner is easy to stub in tests).
export type JobRunner = (job: Job) => Promise<{ ok: number; failed: number }>;

export class JobScheduler {
  private timer: NodeJS.Timeout | null = null;

  constructor(
    private readonly store: JobStore,
    private readonly runner: JobRunner,
    private readonly tickMs = 30_000,
  ) {}

  start(now: number = Date.now()): void {
    // Arm daily jobs whose time already passed today so a CMS restart doesn't
    // "catch up" and fire them immediately on boot.
    const day = Math.floor(now / 86_400_000);
    const minuteOfDay = Math.floor((now % 86_400_000) / 60_000);
    for (const job of this.store.list()) {
      if (job.schedule.mode === 'daily' && job.lastDailyDay == null &&
          job.schedule.dailyMinute != null && minuteOfDay >= job.schedule.dailyMinute) {
        job.lastDailyDay = day;
        this.store.upsert(job);
      }
    }
    this.timer = setInterval(() => { void this.tick(); }, this.tickMs);
    this.timer.unref?.();
  }

  stop(): void {
    if (this.timer) { clearInterval(this.timer); this.timer = null; }
  }

  async tick(now: number = Date.now()): Promise<void> {
    for (const job of dueJobs(this.store.list(), now)) {
      await this.runOne(job, now);
    }
  }

  // Run a single job immediately (also used by the "run now" REST endpoint).
  async runOne(job: Job, now: number = Date.now()): Promise<{ ok: number; failed: number }> {
    let result = { ok: 0, failed: 0 };
    try {
      result = await this.runner(job);
    } catch {
      result = { ok: 0, failed: 1 };
    }
    markRan(job, now, result);
    this.store.upsert(job);
    return result;
  }
}
