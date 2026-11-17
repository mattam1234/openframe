import { DeviceRegistry } from './registry';
import { JsonStore } from './store';
import { Device } from './types';

// Keep the devices snapshot on disk in step with the registry.
//
// Heartbeats fire 'change' constantly and their state is re-learned from the
// devices after a restart (which are restored offline anyway), so those writes
// are coalesced to at most one per `heartbeatDelayMs`. Operator edits (tags,
// notes, site) exist nowhere else, so they are written immediately: a crash
// right after an edit must not lose it.
export function persistDevices(
  registry: DeviceRegistry,
  store: JsonStore<{ devices: Device[] }>,
  heartbeatDelayMs = 5_000,
): void {
  const snapshot = () => ({ devices: registry.list() });
  registry.on('change', () => store.saveDebounced(snapshot, heartbeatDelayMs));
  registry.on('edit', () => {
    store.saveDebounced(snapshot, heartbeatDelayMs);
    store.flush();
  });
}
