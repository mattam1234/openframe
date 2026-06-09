import { JsonStore } from './store';
import { Device } from './types';

export interface Sample {
  t: number;               // epoch ms
  freeHeap?: number;
  rssi?: number;
  cpuLoadPercent?: number;
}

// Per-device rolling telemetry window. State-only and capped, so it stays small
// for a 6–20 node fleet; persisted so charts survive a restart.
export class HistoryStore {
  private readonly series = new Map<string, Sample[]>();
  private readonly lastSeen = new Map<string, number>();

  constructor(
    private readonly store: JsonStore<{ series: Record<string, Sample[]> }>,
    private readonly maxSamples: number,
  ) {
    const saved = store.load();
    if (saved?.series) {
      for (const [id, samples] of Object.entries(saved.series)) this.series.set(id, samples);
    }
  }

  // Record a sample only when a genuinely new heartbeat arrived (lastSeen moved),
  // so presence-only change events don't create duplicate points.
  record(device: Device): boolean {
    if (!device.online || device.lastSeen == null) return false;
    if (this.lastSeen.get(device.deviceId) === device.lastSeen) return false;
    this.lastSeen.set(device.deviceId, device.lastSeen);

    const samples = this.series.get(device.deviceId) ?? [];
    samples.push({
      t: device.lastSeen,
      freeHeap: device.freeHeap,
      rssi: device.rssi,
      cpuLoadPercent: device.cpuLoadPercent,
    });
    if (samples.length > this.maxSamples) samples.splice(0, samples.length - this.maxSamples);
    this.series.set(device.deviceId, samples);
    return true;
  }

  get(deviceId: string): Sample[] {
    return this.series.get(deviceId) ?? [];
  }

  persist(): void {
    this.store.saveDebounced(() => ({ series: Object.fromEntries(this.series) }));
  }
}
