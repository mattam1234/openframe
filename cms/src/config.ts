import path from 'path';

export interface Config {
  port: number;
  mqttUrl: string;
  mqttUsername?: string;
  mqttPassword?: string;
  /** Root MQTT topic the firmware publishes under (default "openframe"). */
  baseTopic: string;
  /** A device with no heartbeat within this window is treated as offline. */
  offlineTimeoutMs: number;
  /** How long to wait for a /cmd/result ack before failing a command. */
  commandTimeoutMs: number;
  /** Directory for the JSON device snapshot. */
  dataDir: string;
  /** Max telemetry samples retained per device (rolling window). */
  historyMaxSamples: number;
  /** Raise a low-heap alert below this many free bytes. */
  alertLowHeapBytes: number;
  /** Raise a weak-signal alert below this RSSI (dBm). */
  alertLowRssiDbm: number;
  /** Base URL devices use to reach this CMS for OTA. REQUIRED to deploy firmware
   *  (never derived from the request Host header — that would be spoofable and
   *  let an attacker redirect the fleet to malicious firmware). */
  publicUrl?: string;
  /** If set, alerts are POSTed to this URL (Slack/Discord/generic webhook). */
  alertWebhookUrl?: string;
  /** If set, the API + dashboard require this shared token (else open on the LAN). */
  authToken?: string;
  /** Optional read-only token: holders may GET but not mutate (viewer role). */
  viewerToken?: string;
}

function num(value: string | undefined, fallback: number): number {
  const n = value !== undefined ? Number(value) : NaN;
  return Number.isFinite(n) ? n : fallback;
}

export function loadConfig(): Config {
  return {
    port: num(process.env.CMS_PORT, 4000),
    mqttUrl: process.env.MQTT_URL || 'mqtt://localhost:1883',
    mqttUsername: process.env.MQTT_USERNAME || undefined,
    mqttPassword: process.env.MQTT_PASSWORD || undefined,
    baseTopic: process.env.BASE_TOPIC || 'openframe',
    // Firmware heartbeat is every 30 s; allow three misses before going offline.
    offlineTimeoutMs: num(process.env.OFFLINE_TIMEOUT_MS, 90_000),
    commandTimeoutMs: num(process.env.COMMAND_TIMEOUT_MS, 10_000),
    dataDir: process.env.DATA_DIR || path.resolve(process.cwd(), 'data'),
    historyMaxSamples: num(process.env.HISTORY_MAX_SAMPLES, 720),
    alertLowHeapBytes: num(process.env.ALERT_LOW_HEAP_BYTES, 20_000),
    alertLowRssiDbm: num(process.env.ALERT_LOW_RSSI_DBM, -85),
    publicUrl: process.env.CMS_PUBLIC_URL || undefined,
    alertWebhookUrl: process.env.ALERT_WEBHOOK_URL || undefined,
    authToken: process.env.CMS_AUTH_TOKEN || undefined,
    viewerToken: process.env.CMS_VIEWER_TOKEN || undefined,
  };
}
