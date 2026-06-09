import fs from 'fs';
import path from 'path';

// Tiny debounced, atomic JSON persistence. Adequate for the current state-only
// fleet snapshot (6–20 nodes); swap for SQLite when historical telemetry lands
// (roadmap Phase D).
export class JsonStore<T> {
  private readonly file: string;
  private timer: NodeJS.Timeout | null = null;

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

  saveDebounced(produce: () => T, delayMs = 500): void {
    if (this.timer) return;
    this.timer = setTimeout(() => {
      this.timer = null;
      this.saveNow(produce());
    }, delayMs);
    this.timer.unref?.();
  }

  saveNow(data: T): void {
    const tmp = `${this.file}.tmp`;
    fs.writeFileSync(tmp, JSON.stringify(data, null, 2));
    fs.renameSync(tmp, this.file);
  }
}
