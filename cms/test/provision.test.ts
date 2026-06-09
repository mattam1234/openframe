import { test } from 'node:test';
import assert from 'node:assert/strict';
import { encodeProvision, decodeProvision, provisionUrl, buildProvisionConfig } from '../src/provision';

test('encode/decode round-trips a config bundle', () => {
  const cfg = { wifi: { networks: [{ ssid: 'Home', password: 'p@ss/+=' }] }, mqtt: { enabled: true, host: 'broker' } };
  const enc = encodeProvision(cfg);
  // base64url is URL-safe: no +, /, or =
  assert.equal(/[+/=]/.test(enc), false);
  assert.deepEqual(decodeProvision(enc), cfg);
});

test('provisionUrl embeds the encoded bundle and is query-safe', () => {
  const url = provisionUrl('192.168.4.1', { mqtt: { host: 'x' } });
  assert.match(url, /^http:\/\/192\.168\.4\.1\/\?provision=[A-Za-z0-9_-]+#\/settings$/);
  const param = new URL(url).searchParams.get('provision')!;
  assert.deepEqual(decodeProvision(param), { mqtt: { host: 'x' } });
});

test('buildProvisionConfig maps operator input to /api/config shape', () => {
  const cfg = buildProvisionConfig({ ssid: 'Net', password: 'pw', mqttHost: 'b', mqttPort: '1884', baseTopic: 'of' }) as any;
  assert.deepEqual(cfg.wifi.networks, [{ ssid: 'Net', password: 'pw' }]);
  assert.equal(cfg.mqtt.enabled, true);
  assert.equal(cfg.mqtt.host, 'b');
  assert.equal(cfg.mqtt.port, 1884);
  assert.equal(cfg.mqtt.base_topic, 'of');
});

test('buildProvisionConfig returns null with nothing to provision', () => {
  assert.equal(buildProvisionConfig({}), null);
  assert.equal(buildProvisionConfig({ password: 'x' }), null);
});

test('mqtt port defaults to 1883 when not numeric', () => {
  const cfg = buildProvisionConfig({ mqttHost: 'b', mqttPort: 'nope' }) as any;
  assert.equal(cfg.mqtt.port, 1883);
});
