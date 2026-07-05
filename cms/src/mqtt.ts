import mqtt, { MqttClient } from 'mqtt';
import { randomUUID } from 'crypto';
import { Config } from './config';
import { DeviceRegistry } from './registry';
import { DeviceLogStore } from './logs';
import { CommandRequest, CommandResult } from './types';

interface Pending {
  resolve: (r: CommandResult) => void;
  reject: (e: Error) => void;
  timer: NodeJS.Timeout;
}

// Bridges the MQTT broker to the registry: ingests presence + heartbeat, and
// issues commands to a device's /cmd topic, correlating the /cmd/result ack by id.
export class MqttBridge {
  private readonly client: MqttClient;
  private readonly pending = new Map<string, Pending>();
  public connected = false;

  constructor(
    private readonly cfg: Config,
    private readonly registry: DeviceRegistry,
    private readonly logs?: DeviceLogStore,
  ) {
    this.client = mqtt.connect(cfg.mqttUrl, {
      username: cfg.mqttUsername,
      password: cfg.mqttPassword,
      reconnectPeriod: 3000,
      clientId: `openframe-cms-${randomUUID().slice(0, 8)}`,
    });

    this.client.on('connect', () => {
      this.connected = true;
      const b = cfg.baseTopic;
      this.client.subscribe([`${b}/+/online`, `${b}/+/status`, `${b}/+/cmd/result`, `${b}/+/log`], { qos: 1 }, (err) => {
        if (err) console.error('[mqtt] subscribe failed:', err.message);
        else console.log(`[mqtt] connected, subscribed under ${b}/+`);
      });
    });
    this.client.on('close', () => { this.connected = false; });
    this.client.on('error', (err) => console.error('[mqtt] error:', err.message));
    this.client.on('message', (topic, payload) => this.onMessage(topic, payload));
  }

  private onMessage(topic: string, payload: Buffer): void {
    // Topic shape: <base>/<deviceId>/<kind>[/...]
    const parts = topic.split('/');
    if (parts.length < 3) return;
    const deviceId = parts[1];
    const kind = parts[2];
    const text = payload.toString('utf8');

    if (kind === 'online') {
      this.registry.applyPresence(deviceId, text.trim() === 'online' ? 'online' : 'offline');
    } else if (kind === 'status') {
      if (!text) return;
      try {
        this.registry.applyStatus(deviceId, JSON.parse(text));
      } catch {
        console.warn(`[mqtt] bad status JSON from ${deviceId}`);
      }
    } else if (kind === 'cmd' && parts[3] === 'result') {
      try {
        this.onResult(JSON.parse(text) as CommandResult);
      } catch {
        /* ignore malformed acks */
      }
    } else if (kind === 'log') {
      // Warning+ log lines the firmware mirrors to <base>/<id>/log (#83) —
      // structured {ts, level, tag, msg} JSON per line.
      if (!text || !this.logs) return;
      try {
        this.logs.append(deviceId, JSON.parse(text));
      } catch {
        /* ignore malformed log lines */
      }
    }
  }

  private onResult(res: CommandResult): void {
    if (!res || typeof res.id !== 'string') return;
    const p = this.pending.get(res.id);
    if (!p) return;
    clearTimeout(p.timer);
    this.pending.delete(res.id);
    p.resolve(res);
  }

  // Graceful shutdown: reject any in-flight commands and close the connection.
  close(): void {
    for (const [, p] of this.pending) {
      clearTimeout(p.timer);
      p.reject(new Error('bridge closing'));
    }
    this.pending.clear();
    this.client.end(true);
  }

  sendCommand(deviceId: string, req: CommandRequest): Promise<CommandResult> {
    const id = randomUUID();
    const topic = `${this.cfg.baseTopic}/${deviceId}/cmd`;
    const message = JSON.stringify({ id, type: req.type, payload: req.payload ?? null });

    return new Promise<CommandResult>((resolve, reject) => {
      const timer = setTimeout(() => {
        this.pending.delete(id);
        reject(new Error('command timed out (no ack on /cmd/result)'));
      }, this.cfg.commandTimeoutMs);
      timer.unref?.();
      this.pending.set(id, { resolve, reject, timer });

      this.client.publish(topic, message, { qos: 1 }, (err) => {
        if (err) {
          clearTimeout(timer);
          this.pending.delete(id);
          reject(err);
        }
      });
    });
  }
}
