import QRCode from 'qrcode';

// A provisioning bundle is a partial device config (same shape as POST /api/config)
// encoded into a URL the device's captive portal reads from `?provision=`. base64url
// keeps it safe as a query value.

export function encodeProvision(config: Record<string, unknown>): string {
  return Buffer.from(JSON.stringify(config)).toString('base64url');
}

export function decodeProvision(encoded: string): unknown {
  return JSON.parse(Buffer.from(encoded, 'base64url').toString('utf8'));
}

export function provisionUrl(apHost: string, config: Record<string, unknown>): string {
  // `?provision=` sits before the hash so window.location.search sees it; `#/settings`
  // lands the operator directly on the captive portal's Settings route (hash router).
  return `http://${apHost}/?provision=${encodeProvision(config)}#/settings`;
}

export function provisionQrDataUrl(url: string): Promise<string> {
  return QRCode.toDataURL(url, { margin: 1, width: 256 });
}

export interface ProvisionInput {
  ssid?: string;
  password?: string;
  mqttHost?: string;
  mqttPort?: number | string;
  baseTopic?: string;
  apHost?: string;
}

// Build the partial config bundle from operator input. Returns null if there's
// nothing to provision.
export function buildProvisionConfig(input: ProvisionInput): Record<string, unknown> | null {
  const config: Record<string, unknown> = {};
  if (input.ssid) {
    config.wifi = { networks: [{ ssid: input.ssid, password: input.password || '' }] };
  }
  if (input.mqttHost) {
    config.mqtt = {
      enabled: true,
      host: input.mqttHost,
      port: Number(input.mqttPort) || 1883,
      base_topic: input.baseTopic || 'openframe',
    };
  }
  return Object.keys(config).length ? config : null;
}
