import fs from 'fs';
import path from 'path';

// Tiny debounced, atomic JSON persistence. Adequate for the current state-only
// fleet snapshot (6–20 nodes); swap for SQLite when historical telemetry lands
// (roadmap Phase D).
export class JsonStore<T> {
  private readonly file: string;
  private timer: NodeJS.Timeout | null = null;
  private pending: (() => T) | null = null;

  constructor(dir: string, name: string) {
    this.file = path.join(dir, name);
    fs.mkdirSync(dir, { recursive: true });
  }

  load(): T | null {
    try {
      return JSON.parse(fs.readFileSync(this.file, 'utf8')) as T;
    } catch {
      return null;
    }
  }

  // Coalesce frequent updates into one write per `delayMs`. Used for
  // machine-written state that changes on every heartbeat, so it is written
  // compact: pretty-printing roughly doubles the bytes and the time the
  // (synchronous) write blocks the event loop. Call flush() before exit.
  saveDebounced(produce: () => T, delayMs = 500): void {
    this.pending = produce;
    if (this.timer) return;
    this.timer = setTimeout(() => this.flush(), delayMs);
    this.timer.unref?.();
  }

  // Write any pending debounced save immediately (e.g. on shutdown).
  flush(): void {
    if (this.timer) { clearTimeout(this.timer); this.timer = null; }
    const produce = this.pending;
    this.pending = null;
    if (produce) this.write(JSON.stringify(produce()));
  }

  // Immediate write, pretty-printed: used for small operator-edited files
  // (templates, jobs, audit) that are worth keeping human-readable.
  saveNow(data: T): void {
    this.write(JSON.stringify(data, null, 2));
  }

  private write(text: string): void {
    const tmp = `${this.file}.tmp`;
    fs.writeFileSync(tmp, text);
    fs.renameSync(tmp, this.file);
  }
}
