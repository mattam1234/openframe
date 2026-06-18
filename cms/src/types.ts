// Shapes shared across the CMS. The heartbeat fields mirror the firmware's
// TelemetryManager payload published to <baseTopic>/<deviceId>/status.

export interface Heartbeat {
  deviceId?: string;
  name?: string;
  board?: string;
  version?: string;
  ip?: string;
  rssi?: number;
  freeHeap?: number;
  uptimeMs?: number;
  cpuLoadPercent?: number;
  activeProfileId?: string;
}

export interface Device extends Heartbeat {
  deviceId: string;
  /** Operator-assigned labels for grouping / bulk targeting (CMS-side). */
  tags: string[];
  /** Free-text operational notes (CMS-side; searchable in the device grid). */
  notes?: string;
  /** Operator-assigned site/location for multi-site grouping (CMS-side, #66). */
  site?: string;
  /** Best-known liveness, derived from presence + heartbeat freshness. */
  online: boolean;
  /** Epoch ms of the last heartbeat (status message). */
  lastSeen: number | null;
  /** Last retained presence value seen on the /online topic. */
  lastPresence: 'online' | 'offline' | null;
  presenceAt: number | null;
  /** Epoch ms this record last changed. */
  updatedAt: number;
}

export interface CommandRequest {
  type: string;
  payload?: unknown;
}

export interface CommandResult {
  id: string;
  type?: string;
  ok: boolean;
  [key: string]: unknown;
}
