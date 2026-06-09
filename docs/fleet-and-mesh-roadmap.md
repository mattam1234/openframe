# OpenFrame — Fleet Management & Multi-Node Roadmap

This document plans three new capability areas requested for OpenFrame:

1. A **Central Management System (CMS)** — see deployed devices, their status, the UIs/layouts they run, and reconfigure them centrally.
2. **Multi-node linking** — connect 6–20 microcontrollers via a peer-to-peer / mesh network.
3. A set of **companion features** these two unlock.

It builds on the existing firmware primitives — MQTT, Home Assistant discovery, OTA, `EventBus`, `VariableManager`, profiles/layouts/screens, and the Vue web UI — rather than reinventing them.

See [implementation-plan.md](implementation-plan.md) and [product-specifications.md](product-specifications.md) for the existing v1.0 work.

---

## Locked decisions

| Decision | Choice | Rationale |
|---|---|---|
| CMS hosting | **Self-hosted server** (Docker/Node + Vue UI) | Scales to the target fleet, keeps history, reachable across the LAN |
| Device ↔ CMS backbone | **MQTT** | Already on every device; birth/LWT gives free presence; config push reuses `/api/config` |
| Device ↔ device transport | **`NodeLink` abstraction, ESP-NOW first, painlessMesh second** | ESP-NOW: low-latency, infra-less, coexists with WiFi-STA. painlessMesh: self-healing for spread-out nodes |
| Target scale | **6–20 nodes** | Favors a self-hosted CMS and a gateway-bridged mesh over an ESP-only coordinator |

---

## Architecture overview

```
                       ┌─────────────────────────────┐
                       │  Self-hosted CMS (Docker)   │
                       │  Node service + Vue UI + DB │
                       └───────────────┬─────────────┘
                                       │ MQTT (+ optional HTTP for blobs)
                       ┌───────────────┴─────────────┐
                       │        MQTT broker          │
                       └───────┬─────────────┬───────┘
                  WiFi-STA     │             │   WiFi-STA
                 ┌─────────────┴──┐      ┌───┴────────────┐
                 │  Gateway node  │      │  Standalone    │
                 │ (mesh ↔ MQTT)  │      │  WiFi node     │
                 └──────┬─────────┘      └────────────────┘
            ESP-NOW /   │   painlessMesh
        ┌───────────────┼────────────────┐
   ┌────┴────┐     ┌────┴────┐      ┌─────┴───┐
   │ Leaf A  │     │ Leaf B  │      │ Leaf C  │   ← no WiFi infra needed
   └─────────┘     └─────────┘      └─────────┘
```

- Nodes with WiFi talk to the CMS directly over MQTT.
- Leaf nodes without WiFi infra join via ESP-NOW/mesh; a **gateway node** bridges mesh ↔ MQTT so the whole cluster is visible in the CMS.
- Remote node data lands in the existing `VariableManager` as `node/<id>/<name>`, so today's Actions and Screens can display another node's data with no new UI.

---

## Phase A — Telemetry & presence contract (firmware) ✅

Foundation for everything; no new transport yet. **Done** — all three ESP targets build green.

- [x] Define MQTT topic contract: `<baseTopic>/<deviceId>/status`, `/online` (retained birth + LWT). `MqttManager::deviceTopic()` / `publishDevice()` namespace every node under its id; `/telemetry`, `/cmd`, `/cmd/result` are reserved in the same space and wired in Phase B.
- [x] Publish a periodic heartbeat (30 s) — `TelemetryManager` publishes retained JSON to `/status`: deviceId, name, board, version, IP, RSSI, free heap, uptime, CPU load, active profile id. Fires immediately on (re)connect, then on the interval.
- [x] Last-Will "offline" message for instant presence detection — set on the CONNECT packet in `MqttManager::connect()` (QoS 1, retained); retained "online" birth published on success.
- [x] Stable per-device `deviceId` — `WiFiManager::deviceId()` (12-hex MAC, cached). Surfaced read-only in the Settings UI and in `/api/status` + `/api/config`. Also now the MQTT client id, so same-named devices no longer collide.
- [x] Version/identity reuse — heartbeat reuses `OF_VERSION_STRING`, board type, and the same fields as `/api/status`.

## Phase B — Self-hosted CMS (server + UI) ✅ (core complete)

Built in [`cms/`](../cms/README.md) and verified end-to-end (in-process broker +
simulated devices): presence/heartbeat ingest, LWT→offline, single + **bulk**
command round-trips, and **template** create/deploy/CRUD all pass. Firmware gained
a `CommandManager` (device half of remote control). Docker compose bundles
Mosquitto for zero-config first run. Remaining polish: a Vue UI port and a richer
drill-in (per-device screen/profile listing).

- [x] New `cms/` service (Node + TypeScript): MQTT client, REST + WebSocket API, JSON-snapshot store (SQLite deferred to Phase D when historical telemetry lands)
- [x] Device registry auto-populated from birth/heartbeat; marked offline on LWT or heartbeat-timeout sweep
- [x] **Fleet grid view**: online/offline, name, id, board, fw version, IP, RSSI, heap, uptime, active profile, last-seen — live over WebSocket
- [x] **Device drill-in**: per-device page (`device.html`) with full live status + the reconfigure actions below
- [x] **Remote reconfigure**: command channel live end-to-end — `POST /api/devices/:id/cmd` → `/cmd` → device `CommandManager` → ack on `/cmd/result`. Commands: `identify`, `get_status`, `reboot`, `activate_profile` (switch displayed profile), `apply_config` (push partial config → merge/save/reboot, reusing the `/api/config` path).
- [x] **Bulk / group push**: `POST /api/commands` fans a command out to selected devices or the whole fleet (online-only optional), returning per-device results. Fleet grid has row-select + a bulk action bar.
- [x] **Template library**: named reusable commands (`/api/templates` CRUD, persisted), deployable to many via `POST /api/templates/:id/deploy`. Managed on the Templates page; deployed to selected devices from the Fleet grid. (A "config template" = `apply_config`; a "profile template" = `activate_profile`.)
- [ ] Reuse the existing Vue UI components — dashboard is currently a lightweight standalone page; Vue port is a later refinement
- [~] Richer drill-in: the drill-in now has a live **profile picker** — `get_profiles` command returns a device's profiles + active id, and you can activate one remotely (verified end-to-end). Screen listing/preview still open.

## Phase C — `NodeLink` transport abstraction (firmware) 🟡 ESP-NOW MVP

First increment built and green on all three targets (`NodeLink.h`,
`NodeLinkManager.cpp`, `EspNowBackend.{h,cpp}`). Addressing is free — a node's
`deviceId` is its STA-MAC in hex, so a 12-char id maps straight to a peer MAC.
Enable + channel are in Settings → "Node Link (mesh)".

- [x] `NodeLink` interface: `send(dstId, type, payload)`, `broadcast(...)`, `onMessage(cb)`, `publishVar(name, value)`, announce-based peer discovery
- [x] **ESP-NOW backend** (default): peer registration, channel handling (0 = follow WiFi channel, else pinned), platform-shimmed recv callbacks (arduino-esp32 2.x vs ESP8266), lock-free recv ring drained in `loop()`. *PMK/LMK encryption not yet wired.*
- [x] Message envelope `{s,d,t,q,p}` = `{srcId, dstId, type, seq, payload}`; types `Announce`,`Heartbeat`,`Var`,`Cmd`,`CmdResult`,`DataReq`,`DataResp` defined (Announce + Var handled; rest reserved)
- [x] Incoming `Var` messages mirrored into `VariableManager` as `node/<srcId>/<name>` (transient) — **delivers "display another ESP's data"**
- [x] **Gateway role** (`GatewayManager`): a node with `nodelink.gateway` + MQTT bridges every ESP-NOW leaf it hears onto `<baseTopic>/<leafId>/{online,status}` (retained), so **leaf nodes appear in the CMS** with `link: "esp-now"` + `via: <gatewayId>`. Leaves are marked offline locally after ~45 s of silence. Verified end-to-end at the CMS (simulated gateway → leaf shows online with board/version, then offline).
- [ ] **painlessMesh backend** (second): same `INodeLinkBackend` interface, for larger self-healing networks
- [ ] ESP-NOW payload encryption (PMK/LMK) and per-message ack/retry; richer leaf heartbeat (heap/uptime over `NodeLink`)

## Phase D — Companion features

- [x] **Cross-node distributed automations** — new `remote_action` step type: an action on node A sends a NodeLink `Cmd` (`trigger=<id>`) to node B, which runs the named local action (which can drive an output, set a variable, etc.). Receiver only acts on messages addressed to it. Editable in the Actions UI (target node id + remote action id). Builds green on all three targets. *(Firmware path is build-verified, not bench-tested.)*
- [ ] **Synchronized displays / LED effects** across nodes (coordinated animations, video-wall)
- [ ] **Cluster time sync** (NTP via gateway, or beacon for infra-less clusters)
- [ ] **Coordinator/gateway failover & election**
- [~] **Mesh topology map** in the CMS — a Topology page renders the broker → direct WiFi nodes, and gateways with their ESP-NOW leaves nested (derived from each leaf's `via`), live over WebSocket. Verified rendering in a real browser (Playwright). Per-link RSSI not shown (the ESP-NOW core in use doesn't expose receive RSSI); remote display mirroring / live preview still open.
- [x] **Fleet-wide OTA** — the CMS hosts firmware binaries (`POST/GET/DELETE /api/firmware`, served at `/firmware/<file>`) and a `POST /api/firmware/:id/deploy` fans an `ota_url` command to selected/tagged/all devices; the firmware `CommandManager` downloads + flashes via the platform HTTP-update lib (plain HTTP from the CMS → no device-side TLS). Version tracking is the existing heartbeat `version` (watch the Fleet grid); staged rollout via tag/selection targeting. Firmware page in the CMS. Rollback relies on the ESP32 dual-partition scheme. Verified end-to-end at the CMS (upload → serve → deploy → device gets the right URL). *(Device-side flash is build-verified only.)* **Security:** the OTA base URL must be set via `CMS_PUBLIC_URL` (never derived from the request Host header, which would be spoofable → fleet RCE). Follow-up hardening: HTTPS from a pinned host + signed-image verification on the device before flashing.
- [~] **Device groups & tags, bulk operations** — done: CMS-side tags (`PUT /api/devices/:id/tags`), tag-based targeting for bulk commands + template deploys, fleet-grid tag column + filter, drill-in tag editor. Still open: secure node provisioning/onboarding (AP/QR pairing).
- [x] **Historical telemetry + alerting** in the CMS — per-device rolling telemetry window (`/api/devices/:id/history`, sparklines on the drill-in) and an alert engine (offline + low-heap + low-RSSI thresholds) with a live **Alerts** page, nav badge, and `/api/alerts`. Verified end-to-end (raise/resolve on heartbeat + presence). *(Other Phase D items below remain open.)*

---

## Suggested sequencing

1. **Phase A** (small, high-value): presence + heartbeat over existing MQTT — immediately enables a basic fleet view.
2. **Phase B** MVP: read-only fleet grid, then remote reconfigure.
3. **Phase C** ESP-NOW backend + `VariableManager` integration: cross-node data display.
4. **Phase C** gateway bridge: leaf nodes visible in CMS.
5. **Phase D** features, prioritized by need (distributed automations and fleet OTA are typically highest value).

## Open questions

- ~~MQTT broker: bundle one or assume the user's existing broker?~~ **Resolved (Phase B):** bundle Mosquitto in the CMS docker-compose for zero-config first run; drop the service and set `MQTT_URL` to use an existing broker.
- ~~CMS auth model (LAN-trusted vs login)?~~ **Resolved for MVP:** LAN-trusted, anonymous broker, no dashboard auth. Hardening (broker auth + dashboard auth behind a proxy) deferred; remote/cloud access not required yet.
- ESP-NOW vs WiFi channel coexistence: pin all nodes to the AP channel, or run gateway nodes WiFi-only and leaves ESP-NOW-only? *(open — decide before Phase C)*
