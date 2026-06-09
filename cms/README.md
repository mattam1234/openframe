# OpenFrame CMS

A self-hosted **Central Management System** for an OpenFrame fleet. It subscribes
to the devices' MQTT presence/heartbeat topics, keeps a live registry, serves a
fleet dashboard, and can send remote commands to a device.

This implements **Phase B (MVP)** of [the fleet roadmap](../docs/fleet-and-mesh-roadmap.md):
read-only fleet grid + basic remote commands. It builds on the Phase A firmware
telemetry contract.

## What it does today

- **Ingest** — subscribes to `openframe/+/online` (retained presence) and
  `openframe/+/status` (retained heartbeat).
- **Registry** — auto-populates from birth/heartbeat, marks devices offline on
  Last-Will or heartbeat timeout. Persisted to `data/devices.json`.
- **Fleet grid** — live web dashboard (online/offline, name, id, board, fw, IP,
  RSSI, heap, uptime, active profile, last-seen) with WebSocket push. Click a
  device for the **drill-in** page.
- **Remote commands** — `POST /api/devices/:id/cmd` publishes to
  `openframe/<id>/cmd` and resolves when the device acks on `.../cmd/result`.
  Commands: `identify`, `get_status`, `reboot`, `activate_profile`
  (`{ id }`), and `apply_config` (a partial config object → the device merges,
  saves, and reboots). The drill-in page exposes all of these.
- **Bulk / group push** — `POST /api/commands` fans one command out to selected
  devices, devices matching a tag, or the whole fleet at once. The fleet grid has
  row-select, a tag filter, and a bulk bar.
- **Tags / groups** — assign operator labels to devices (fleet grid shows them;
  edit on the drill-in) and target bulk commands or template deploys by tag.
- **Template library** — named, reusable commands (CRUD under `/api/templates`,
  persisted to `data/templates.json`). Manage them on the **Templates** page and
  deploy to selected devices from the **Fleet** page.
- **ESP-NOW leaves** — nodes without WiFi appear automatically when a gateway
  node bridges them onto MQTT. They show with sparse fields plus `link: esp-now`
  and `via: <gatewayId>` (visible on the drill-in page).
- **History & alerts** — a rolling per-device telemetry window (heap/RSSI/CPU)
  drives sparklines on the drill-in, and an alert engine raises/resolves
  offline + low-heap + low-RSSI alerts, shown live on the **Alerts** page with a
  nav badge.
- **Alert webhooks** — set `ALERT_WEBHOOK_URL` to POST every alert (raise and
  resolve) to Slack/Discord/a generic endpoint.
- **Fleet-wide OTA** — upload a firmware `.bin` on the **Firmware** page; the CMS
  hosts it over plain HTTP and a deploy fans an `ota_url` command to the targets,
  which download + flash it. Watch the Fleet version column for rollout progress.
- **Topology** — a page showing how the fleet connects: direct WiFi nodes and
  gateways with their ESP-NOW leaves nested beneath them.
- **Provisioning QR** — the **Provision** page (`POST /api/provision`) makes an
  onboarding QR; scan it on a phone joined to a new device's AP and the captive
  portal opens pre-filled with the WiFi + MQTT settings to review and save.

## Quick start (Docker — bundled broker)

```bash
cd cms
docker compose up --build
```

Then point each device's MQTT host (Settings → MQTT) at this machine's IP,
port `1883`, and open the dashboard at <http://localhost:4000>.

## Quick start (local dev)

Requires a reachable MQTT broker (e.g. `docker run -p 1883:1883 eclipse-mosquitto:2`).

```bash
cd cms
npm install
npm run dev          # tsx watch, or: npm run build && npm start
```

Open <http://localhost:4000>.

## Tests

```bash
npm test
```

A `node:test` suite covers the registry (presence/sweep edge cases, tags),
alerts (raise/resolve), templates, history, firmware-store (incl. path-traversal
safety), and an end-to-end integration test (in-process MQTT broker + simulated
device) for ingest, command round-trip, `get_profiles`, bulk/tag targeting,
template + firmware deploy, and the OTA Host-header security guard.

## Configuration

Environment variables (see `.env.example`):

| Var | Default | Meaning |
|---|---|---|
| `CMS_PORT` | `4000` | HTTP/WebSocket port |
| `MQTT_URL` | `mqtt://localhost:1883` | Broker URL |
| `MQTT_USERNAME` / `MQTT_PASSWORD` | — | Broker auth (if required) |
| `BASE_TOPIC` | `openframe` | Must match the devices' MQTT base topic |
| `OFFLINE_TIMEOUT_MS` | `90000` | Heartbeat staleness window |
| `COMMAND_TIMEOUT_MS` | `10000` | Ack wait for remote commands |
| `DATA_DIR` | `./data` | Snapshot location |
| `HISTORY_MAX_SAMPLES` | `720` | Telemetry samples retained per device |
| `ALERT_LOW_HEAP_BYTES` | `20000` | Low-heap alert threshold |
| `ALERT_LOW_RSSI_DBM` | `-85` | Weak-signal alert threshold |
| `CMS_PUBLIC_URL` | _(required for OTA)_ | Base URL devices use to fetch OTA firmware (e.g. `http://192.168.1.50:4000`). Firmware deploy is refused if unset — it is **not** derived from the request host, to prevent fleet-wide malicious-firmware redirection |
| `ALERT_WEBHOOK_URL` | _(disabled)_ | If set, every alert (raise + resolve) is POSTed here as JSON |
| `CMS_AUTH_TOKEN` | _(open)_ | If set, the API + dashboard require this shared token |

## API

- `GET /api/health` → `{ ok, mqtt, devices }`
- `GET /api/devices` → `{ devices: [...] }`
- `GET /api/devices/:id` → device record (404 if unknown)
- `POST /api/devices/:id/cmd` `{ "type": "identify" }` → the device's ack, or
  `504` on timeout. Commands: `identify`, `get_status`, `get_profiles`
  (returns `{ active, profiles: [{ id, name }] }`), `reboot`,
  `activate_profile` (`{ "type": "activate_profile", "payload": { "id": "..." } }`),
  `apply_config` (`{ "type": "apply_config", "payload": { ...partial config... } }`),
  `ota_url` (`{ "type": "ota_url", "payload": { "url": "..." } }`).
- `POST /api/commands` `{ type, payload?, deviceIds?, tags?, onlineOnly? }` →
  fan-out to many; returns `{ count, ok, results: [...] }`. Target precedence:
  `deviceIds` → devices matching any of `tags` → the whole fleet (`onlineOnly`
  filters the latter two).
- `PUT /api/devices/:id/tags` `{ tags: [...] }` → set a device's tags (deduped);
  returns the updated device, `404` if unknown.
- `GET /api/templates` · `POST /api/templates` · `PUT /api/templates/:id` ·
  `DELETE /api/templates/:id` — template CRUD. A template is
  `{ name, description?, command: { type, payload? } }`.
- `POST /api/templates/:id/deploy` `{ deviceIds?, tags?, onlineOnly? }` → deploys
  the template's command to the targets (same targeting + result shape as
  `/api/commands`).
- `GET /api/devices/:id/history` → `{ samples: [{ t, freeHeap, rssi, cpuLoadPercent }] }`.
- `GET /api/alerts` → `{ active: [...], recent: [...] }`.
- `POST /api/provision` `{ ssid?, password?, mqttHost?, mqttPort?, baseTopic?, apHost? }`
  → `{ url, qr (PNG data URL), config }` for device onboarding.
- `GET /api/firmware` · `POST /api/firmware` (multipart `file`) ·
  `DELETE /api/firmware/:name` — firmware binary library; binaries served at
  `GET /firmware/:name`.
- `POST /api/firmware/:name/deploy` `{ deviceIds?, tags?, onlineOnly? }` → sends
  an `ota_url` command pointing at this CMS to the targets.
- `GET /ws` → WebSocket; on connect sends a device `snapshot` + `alerts`; then
  streams `device` and `alert` deltas.

## Security

Ships LAN-trusted with anonymous MQTT and **no dashboard auth by default** —
intended for a trusted network. To require auth, set `CMS_AUTH_TOKEN`: the API
and dashboard then demand that shared token (sent as `Authorization: Bearer`
or via an HttpOnly login cookie set by `POST /api/login`; the dashboard shows a
login overlay). `GET /api/health` and firmware downloads at `/firmware/:name`
stay public (devices have no token). Comparison is constant-time, and login
attempts are rate-limited (10/min per client) to resist brute force. For broker
auth, see `mosquitto/mosquitto.conf`; for stronger exposure, also put the CMS
behind a reverse proxy with TLS.

The server shuts down gracefully on SIGINT/SIGTERM (closes the MQTT connection),
so `docker stop` leaves a clean last-will/disconnect.

**OTA hardening.** Firmware deploy refuses to run unless `CMS_PUBLIC_URL` is set,
and the OTA URL is never taken from the request `Host` header — otherwise a
spoofed/rebound host could redirect the whole fleet to attacker-hosted firmware
(remote code execution). For a stronger posture, serve firmware over HTTPS from a
pinned host and sign the images so devices verify them before flashing (the
firmware does not yet verify image signatures — a known follow-up).

## Not yet

- Richer drill-in: the profile picker is done (`get_profiles` + remote activate);
  screen listing/preview is still open.
- Port the dashboard to the Vue stack to reuse the device UI components.
- SQLite + historical telemetry (Phase D).
