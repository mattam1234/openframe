import { EventEmitter } from 'events';
import { Device, Heartbeat } from './types';

// In-memory fleet registry. Emits 'change' (with the updated Device) whenever a
// device's record changes, so the WebSocket layer and the persistence layer can
// react without polling.
export class DeviceRegistry extends EventEmitter {
  private readonly devices = new Map<string, Device>();

  constructor(private readonly offlineTimeoutMs: number) {
    super();
  }

  list(): Device[] {
    return [...this.devices.values()].sort((a, b) => a.deviceId.localeCompare(b.deviceId));
  }

  get(id: string): Device | undefined {
    return this.devices.get(id);
  }

  // Restore persisted devices on boot, but never trust persisted liveness —
  // a device is offline until we hear a fresh heartbeat or presence message.
  loadSnapshot(devices: Device[]): void {
    for (const d of devices) {
      this.devices.set(d.deviceId, { ...d, tags: d.tags ?? [], online: false });
    }
  }

  private ensure(id: string): Device {
    let d = this.devices.get(id);
    if (!d) {
      d = {
        deviceId: id,
        tags: [],
        online: false,
        lastSeen: null,
        lastPresence: null,
        presenceAt: null,
        updatedAt: Date.now(),
      };
      this.devices.set(id, d);
    }
    return d;
  }

  // Set a device's tags (deduped, trimmed). Returns false if the device is unknown.
  setTags(id: string, tags: string[]): boolean {
    const d = this.devices.get(id);
    if (!d) return false;
    const clean = [...new Set(tags.map((t) => String(t).trim()).filter(Boolean))];
    d.tags = clean;
    d.updatedAt = Date.now();
    this.emit('change', d);
    return true;
  }

  // Set a device's free-text notes. Returns false if the device is unknown.
  setNotes(id: string, notes: string): boolean {
    const d = this.devices.get(id);
    if (!d) return false;
    d.notes = String(notes);
    d.updatedAt = Date.now();
    this.emit('change', d);
    return true;
  }

  applyStatus(id: string, hb: Heartbeat): void {
    const d = this.ensure(id);
    const now = Date.now();
    Object.assign(d, hb, { deviceId: id, lastSeen: now, online: true, updatedAt: now });
    this.emit('change', d);
  }

  applyPresence(id: string, state: 'online' | 'offline'): void {
    const d = this.ensure(id);
    const now = Date.now();
    d.lastPresence = state;
    d.presenceAt = now;
    d.online = state === 'online';
    d.updatedAt = now;
    this.emit('change', d);
  }

  // Mark devices offline if their heartbeat has gone stale. The Last-Will gives
  // instant offline detection in the common case; this catches the case where a
  // node vanishes without the broker noticing (e.g. broker restart). Heartbeat
  // staleness is the sole criterion here — an explicit LWT already flips a device
  // offline immediately via applyPresence(). Devices known only from a retained
  // "online" birth (no heartbeat yet, lastSeen null) are left to the LWT.
  sweep(): void {
    const now = Date.now();
    for (const d of this.devices.values()) {
      if (d.online && d.lastSeen != null && now - d.lastSeen > this.offlineTimeoutMs) {
        d.online = false;
        d.updatedAt = now;
        this.emit('change', d);
      }
    }
  }
}
