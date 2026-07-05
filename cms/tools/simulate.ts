/**
 * Fleet simulator — publishes N fake devices' presence + heartbeat over MQTT so
 * the CMS dashboard, topology, history sparklines, alerts, and bulk/command UI
 * can be exercised without any real hardware.
 *
 *   npm run simulate            # 5 devices on mqtt://localhost:1883
 *   COUNT=20 MQTT_URL=mqtt://broker:1883 BASE_TOPIC=openframe npm run simulate
 *
 * Each device publishes a retained "online" birth + retained JSON /status
 * heartbeat every few seconds (matching the firmware TelemetryManager contract),
 * with metrics that drift over time so charts/alerts have something to show.
 * It also answers /cmd by echoing an ack on /cmd/result (apply_config acks carry
 * restartRequired per the firmware's hot-apply contract), and emits an
 * occasional Warning+ line on /log so the device-log panel has traffic.
 * Ctrl-C publishes "offline" (LWT-style) and exits.
 */
import mqtt from 'mqtt';

const MQTT_URL = process.env.MQTT_URL || 'mqtt://localhost:1883';
const BASE = process.env.BASE_TOPIC || 'openframe';
const COUNT = Math.max(1, parseInt(process.env.COUNT || '5', 10) || 5);
const HEARTBEAT_MS = Math.max(1000, parseInt(process.env.HEARTBEAT_MS || '5000', 10) || 5000);
const BOARDS = ['ESP32', 'ESP32-S3', 'ESP8266'];

interface SimDevice {
  id: string;
  name: string;
  board: string;
  heap: number;
  rssi: number;
  bootMs: number;
}

function makeDevices(n: number): SimDevice[] {
  const out: SimDevice[] = [];
  for (let i = 0; i < n; i++) {
    // Deterministic 12-hex id so restarts reuse the same fleet entries.
    const id = `sim${String(i).padStart(2, '0')}`.padEnd(12, '0');
    out.push({
      id,
      name: `Sim Device ${i + 1}`,
      board: BOARDS[i % BOARDS.length],
      heap: 200_000 - i * 5_000,
      rssi: -45 - (i % 40),
      bootMs: Date.now() - i * 60_000,
    });
  }
  return out;
}

const devices = makeDevices(COUNT);
const client = mqtt.connect(MQTT_URL, { clientId: `openframe-sim-${Date.now()}` });

function statusTopic(id: string) { return `${BASE}/${id}/status`; }
function onlineTopic(id: string) { return `${BASE}/${id}/online`; }

function publishHeartbeat(d: SimDevice): void {
  // Drift metrics a little so sparklines/alerts move.
  d.heap = Math.max(20_000, d.heap + Math.round((Math.sin(Date.now() / 9000 + d.id.length) * 4000)));
  d.rssi = Math.max(-95, Math.min(-30, d.rssi + (Date.now() % 3) - 1));
  const hb = {
    deviceId: d.id,
    name: d.name,
    board: d.board,
    version: '0.1.0-sim',
    ip: `192.168.1.${50 + (parseInt(d.id.slice(3, 5), 10) || 0)}`,
    rssi: d.rssi,
    freeHeap: d.heap,
    uptimeMs: Date.now() - d.bootMs,
    cpuLoadPercent: 5 + (Date.now() / 1000 % 30 | 0),
    activeProfileId: '',
    // Build-time feature flags, roughly matching the constrained-board gating.
    features: { weather: d.board !== 'ESP8266', push: true, tls: d.board !== 'ESP8266' },
  };
  client.publish(statusTopic(d.id), JSON.stringify(hb), { qos: 1, retain: true });
}

// Warning+ log lines, matching the firmware's <base>/<id>/log mirror (#83).
const LOG_LINES: Array<{ level: string; tag: string; msg: string }> = [
  { level: 'warning', tag: 'WiFi',    msg: 'RSSI low (-82 dBm), roaming candidates: 0' },
  { level: 'warning', tag: 'Weather', msg: 'Open-Meteo returned HTTP 429' },
  { level: 'error',   tag: 'Push',    msg: 'Push via ntfy failed: HTTP 502' },
  { level: 'warning', tag: 'Sensors', msg: "Sensor 'bme280' read failed: I2C timeout" },
  { level: 'warning', tag: 'MQTT',    msg: 'MQTT connect failed: Connection timeout (rc=-4) — retry in 8s' },
];

function publishLog(d: SimDevice): void {
  const line = LOG_LINES[Math.floor(Math.random() * LOG_LINES.length)];
  client.publish(`${BASE}/${d.id}/log`, JSON.stringify({ ts: Date.now(), ...line }), { qos: 0 });
}

client.on('connect', () => {
  console.log(`[sim] connected to ${MQTT_URL}; simulating ${COUNT} device(s) under ${BASE}/`);

  // Birth + first heartbeat.
  for (const d of devices) {
    client.publish(onlineTopic(d.id), 'online', { qos: 1, retain: true });
    publishHeartbeat(d);
  }

  // Answer commands with an ack so the UI's round-trips resolve.
  client.subscribe(`${BASE}/+/cmd`, { qos: 1 });
  client.on('message', (topic, payload) => {
    const parts = topic.split('/');
    if (parts[2] !== 'cmd') return;
    const id = parts[1];
    try {
      const cmd = JSON.parse(payload.toString('utf8'));
      const ack: Record<string, unknown> = { id: cmd.id, type: cmd.type, ok: true };
      if (cmd.type === 'apply_config') {
        // Mirror the firmware's hot-apply contract: pushes touching only the
        // mqtt/time/notify/weather sections apply live; anything else "reboots".
        const HOT = new Set(['mqtt', 'time', 'notify', 'weather']);
        const keys = Object.keys((cmd.payload as Record<string, unknown>) ?? {});
        ack.restartRequired = !keys.length || !keys.every((k) => HOT.has(k));
      }
      client.publish(`${BASE}/${id}/cmd/result`, JSON.stringify(ack), { qos: 1 });
      console.log(`[sim] ${id} acked '${cmd.type}'`);
    } catch { /* ignore */ }
  });

  setInterval(() => { for (const d of devices) publishHeartbeat(d); }, HEARTBEAT_MS);
  // One random device grumbles every ~15 s so the log panel has traffic.
  setInterval(() => publishLog(devices[Math.floor(Math.random() * devices.length)]), 15_000);
});

client.on('error', (e) => console.error('[sim] mqtt error:', e.message));

function shutdown(): void {
  console.log('\n[sim] going offline…');
  for (const d of devices) client.publish(onlineTopic(d.id), 'offline', { qos: 1, retain: true });
  setTimeout(() => { client.end(true); process.exit(0); }, 300);
}
process.on('SIGINT', shutdown);
process.on('SIGTERM', shutdown);
