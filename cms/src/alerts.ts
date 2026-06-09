import { EventEmitter } from 'events';
import { Device } from './types';

export type AlertType = 'offline' | 'low_heap' | 'low_rssi';
export type Severity = 'warning' | 'critical';

export interface Alert {
  id: string;            // `${deviceId}:${type}`
  deviceId: string;
  type: AlertType;
  severity: Severity;
  message: string;
  raisedAt: number;
  resolvedAt: number | null;
}

export interface AlertThresholds {
  lowHeapBytes: number;
  lowRssiDbm: number;
}

// Derives alerts from device state. Each (device, type) pair is at most one active
// alert; re-evaluating raises or resolves it. Emits 'alert' (the changed Alert)
// for the WebSocket layer. Keeps a bounded recent-history for the UI.
export class AlertManager extends EventEmitter {
  private readonly active = new Map<string, Alert>();
  private readonly recent: Alert[] = [];
  private static readonly RECENT_MAX = 200;

  constructor(private readonly thresholds: AlertThresholds) {
    super();
  }

  activeAlerts(): Alert[] {
    return [...this.active.values()].sort((a, b) => b.raisedAt - a.raisedAt);
  }

  recentAlerts(): Alert[] {
    return [...this.recent].reverse();
  }

  // Re-evaluate every rule for a device after its state changed.
  evaluate(device: Device): void {
    const online = device.online;

    this.apply(device.deviceId, 'offline', 'critical', !online, 'Device is offline');

    // Metric-based rules only make sense while online (offline metrics are stale).
    const lowHeap = online && device.freeHeap != null && device.freeHeap < this.thresholds.lowHeapBytes;
    this.apply(device.deviceId, 'low_heap', 'warning', lowHeap,
      `Free heap ${device.freeHeap} B below ${this.thresholds.lowHeapBytes} B`);

    const lowRssi = online && device.rssi != null && device.rssi !== 0 && device.rssi < this.thresholds.lowRssiDbm;
    this.apply(device.deviceId, 'low_rssi', 'warning', lowRssi,
      `RSSI ${device.rssi} dBm below ${this.thresholds.lowRssiDbm} dBm`);
  }

  private apply(deviceId: string, type: AlertType, severity: Severity, condition: boolean, message: string): void {
    const id = `${deviceId}:${type}`;
    const existing = this.active.get(id);

    if (condition && !existing) {
      const alert: Alert = { id, deviceId, type, severity, message, raisedAt: Date.now(), resolvedAt: null };
      this.active.set(id, alert);
      this.pushRecent(alert);
      this.emit('alert', alert);
    } else if (!condition && existing) {
      existing.resolvedAt = Date.now();
      this.active.delete(id);
      this.pushRecent(existing);
      this.emit('alert', existing);
    }
  }

  private pushRecent(alert: Alert): void {
    this.recent.push({ ...alert });
    if (this.recent.length > AlertManager.RECENT_MAX) {
      this.recent.splice(0, this.recent.length - AlertManager.RECENT_MAX);
    }
  }
}
