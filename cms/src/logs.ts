import { EventEmitter } from 'events';

export interface DeviceLogEntry {
  /** Epoch ms the CMS received the line. */
  t: number;
  /** Device-side timestamp, as sent (millis or epoch depending on firmware clock). */
  ts?: number;
  level: string;   // trace|debug|info|warning|error|fatal
  tag?: string;
  msg: string;
}

// Per-device rolling window of the Warning+ log lines devices mirror to
// <base>/<id>/log (#83). In-memory only: these are live diagnostics, not
// history worth disk writes — a CMS restart starts the window fresh. Emits
// ('entry', deviceId, entry) so the server can stream lines over the WebSocket.
export class DeviceLogStore extends EventEmitter {
  private readonly lines = new Map<string, DeviceLogEntry[]>();

  constructor(private readonly maxLines = 250) {
    super();
  }

  append(deviceId: string, raw: unknown): DeviceLogEntry | null {
    if (!raw || typeof raw !== 'object') return null;
    const r = raw as Record<string, unknown>;
    if (typeof r.msg !== 'string' || !r.msg) return null;
    const entry: DeviceLogEntry = {
      t: Date.now(),
      ts: typeof r.ts === 'number' ? r.ts : undefined,
      level: typeof r.level === 'string' ? r.level : 'warning',
      tag: typeof r.tag === 'string' ? r.tag : undefined,
      msg: r.msg,
    };
    const buf = this.lines.get(deviceId) ?? [];
    buf.push(entry);
    if (buf.length > this.maxLines) buf.splice(0, buf.length - this.maxLines);
    this.lines.set(deviceId, buf);
    this.emit('entry', deviceId, entry);
    return entry;
  }

  get(deviceId: string): DeviceLogEntry[] {
    return this.lines.get(deviceId) ?? [];
  }
}
