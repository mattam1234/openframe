import { JsonStore } from './store';

// One recorded fleet action: who (role), what, against which target, the result,
// and when. Kept as a capped, persisted ring so it survives a CMS restart.
export interface AuditEntry {
  t: number;          // epoch ms
  role: string;       // 'admin' | 'viewer' | 'open' (auth disabled)
  action: string;     // e.g. 'device.cmd', 'commands.bulk', 'template.deploy', 'firmware.deploy'
  target?: string;    // device id / template id / firmware name
  detail?: string;    // command type or note
  ok?: boolean;       // overall success
  count?: number;     // affected devices, for fan-outs
}

export class AuditLog {
  private readonly entries: AuditEntry[] = [];

  constructor(
    private readonly store: JsonStore<{ entries: AuditEntry[] }>,
    private readonly max = 500,
  ) {
    const saved = store.load();
    if (saved?.entries) this.entries.push(...saved.entries.slice(-max));
  }

  record(e: Omit<AuditEntry, 't'>): void {
    this.entries.push({ t: Date.now(), ...e });
    if (this.entries.length > this.max) this.entries.splice(0, this.entries.length - this.max);
    // Write synchronously: audit entries are low-volume and must not be lost to a
    // crash between a debounced flush.
    this.store.saveNow({ entries: this.entries });
  }

  // Newest first.
  list(): AuditEntry[] {
    return this.entries.slice().reverse();
  }
}
