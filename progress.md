# OpenFrame — Feature Backlog (100 items)

A working backlog of features worth integrating into OpenFrame, grouped by area.
Items already tracked as open/in-progress in
[`docs/fleet-and-mesh-roadmap.md`](docs/fleet-and-mesh-roadmap.md) are marked **(roadmap)**.

Legend: `- [ ]` not started · `- [~]` partial · `- [x]` done · **P1** high value / foundational · **P2** solid · **P3** nice-to-have / larger effort

---

## Firmware & Core Runtime

- [x] 1. **P1** Config schema versioning + auto-migration on boot — *survive old LittleFS JSON formats without bricking saved configs.*
- [x] 2. **P1** Atomic config writes (write-temp-then-rename) — *no more corrupt config on power loss mid-save.*
- [x] 3. **P2** Config backup/restore slots with one-tap rollback — *undo a bad config push instantly.* (auto-snapshot before every config apply + manual "back up now"; ring of 5 in /cfgbak; REST create/list/restore/delete (restore validates JSON, snapshots current first, reboots); Settings UI list with Restore/Delete)
- [x] 4. **P2** Factory-reset long-press that wipes config but keeps WiFi — *recover a misconfigured device in the field.* (ConfigManager.factoryResetKeepWifi; boot-time GPIO long-press via opt-in device.reset_pin/hold_ms; gated POST /api/factory-reset (snapshots first); Settings two-step danger button)
- [x] 5. **P1** Watchdog + safe-mode boot after repeated crash loops — *device stays reachable instead of dying silently.*
- [x] 6. **P2** Crash/panic backtrace stored to LittleFS, surfaced in UI — *diagnose resets without a serial cable.* (HealthMonitor logs panic/watchdog/brownout resets to /crashlog.json (rolling 20, crash-only to spare flash); GET /api/crashlog; Dashboard "Recent crash resets" panel. ⚑ full stack backtrace would need an ESP32 coredump partition — partition-table change deferred.)
- [ ] 7. **P3** Deep-sleep / light-sleep power profiles — *unlocks battery-powered nodes.*
- [x] 8. **P2** Brownout detection + state persist before reset — *don't lose state on flaky power.* (periodic dirty-flush of persistent variables every 30s so an unexpected reset loses ≤1 interval; brownout resets detected + logged via [[#6]] crashlog. Note: true persist-*during*-brownout isn't reliable in the ISR, so periodic flush is the robust approach.)
- [ ] 9. **P3** Boot-time self-test (RAM, flash, I2C scan) with report — *catch bad hardware early.*
- [x] 10. **P1** Local cron/scheduler engine for time-based actions — *automations without cluster/NTP dependency.* (interval = no clock needed; daily = needs NTP)
- [x] 11. **P2** Typed variables (enums, ranges, units, persist flags) — *richer, validated automation data.* (Variable gains optional min/max range (clamps Int/Float sets), unit suffix, and enum options (rejects out-of-set String sets); (de)serialized in variables.json + exposed via /api/variables. persist flag already existed. Defined via variables.json; a dedicated variable-definition editor UI is a follow-up.)
- [ ] 12. **P3** RTC (DS3231) support — *accurate time with no WiFi/NTP.*

## Hardware Support

- [x] 13. **P2** Stepper driver support (A4988/DRV8825/TMC2209) — *motion projects.* (step/dir driver: STEP=`pin`, DIR=`pin_dir`, optional active-LOW `pin_enable`; non-blocking stepping toward an absolute target in loop() at up to `max_step_hz`; emits `PositionReached`. REST `move`+`position` and action-step `move` (own `position` field, round-trips); LayoutDesigner type + DIR-pin field; OutputControlCard jog/target control. ⚑ microstepping/accel ramps deferred.)
- [x] 14. **P1** Servo output type with angle/sweep actions — *common, high-demand actuator.*
- [ ] 15. **P2** Encoder-with-button composite input — *menu navigation in one control.*
- [ ] 16. **P3** ESP32 native capacitive touch pins as input — *touch buttons with no extra hardware.*
- [ ] 17. **P2** More sensors: SCD40/41, SGP30, VL53L0X, MAX6675 — *air quality, ToF, thermocouple coverage.*
- [ ] 18. **P3** Load cell + HX711 (weight/force) — *scales and force sensing.*
- [x] 19. **P1** Addressable-LED effect presets (rainbow, chase, breathe, fire) — *instant polish for WS2812 users.* (added Fire effect; rest already existed)
- [ ] 20. **P3** E-paper (Waveshare SPI) display driver — *low-power info panels.*
- [ ] 21. **P3** Matrix keypad input driver — *keypad entry / PIN pads.*
- [ ] 22. **P2** Generic I2C/SPI register-map plugin (add chips via JSON) — *new sensors with no reflash.*
- [ ] 23. **P3** PWM gamma-correction curves for LED/RGB — *visually linear dimming.*

## Connectivity & Protocols

- [x] 24. **P1** mDNS so devices are `openframe-xxxx.local` — *find devices without hunting IPs.*
- [x] 25. **P2** Static IP / DHCP reservation hints in WiFi settings — *stable addressing for the CMS.* (optional `wifi.static_ip`/gateway/subnet/dns1/dns2; WiFi.config() applied before begin() in STA mode, with sane fallbacks (gw=x.x.x.1, dns=gw) and graceful fallback to DHCP if it won't parse/apply. Settings UI Static IP section. Blank IP = DHCP.)
- [ ] 26. **P2** Multi-AP credentials with roaming to strongest network — *resilient connectivity.*
- [ ] 27. **P3** Ethernet (W5500/LAN8720) support — *wired, reliable nodes.*
- [x] 28. **P1** MQTT over TLS with CA cert upload — *secure broker links beyond the LAN.* (TLS client + CA at /mqtt_ca.pem or insecure mode; Settings UI)
- [x] 29. **P1** MQTT reconnect backoff + last-error in UI — *no silent disconnects.*
- [ ] 30. **P3** Matter/Thread bridge investigation — *native smart-home pairing.*
- [ ] 31. **P2** Richer HA entity types (climate, cover, light brightness) — *fuller Home Assistant integration.*
- [x] 32. **P1** Outbound webhook action (POST JSON, templated) — *integrate with anything (Slack, IFTTT, custom).* (HTTP action now interpolates `{{variable}}` in URL + body — see [[#40]])
- [ ] 33. **P3** REST rate limiting + ETag/caching on config — *robustness under load.*
- [x] 34. **P2** NTP pool config + timezone/DST handling — *correct local time everywhere.* (new `time` config: two NTP servers + POSIX TZ string (applied via setenv/tzset). System clock stays UTC; daily schedules now resolve via localtime_r so "daily at HH:MM" fires at local wall-clock with automatic DST. Settings UI: NTP fields + a TZ preset combobox (CET/UK/US/AU/JP/IN…). ⚑ day-rollover key uses tm_year/tm_yday since tm_gmtoff is absent in this newlib.)

## Automation Engine

- [x] 35. **P1** Visual rule builder (trigger → condition → action) in the UI — *automations without hand-editing JSON.* (already provided by the Action Manager: trigger + conditions + ordered steps, all form-driven)
- [x] 36. **P1** Action steps: if/else branch + bounded loop — *real conditional logic.* (if/else/endif + repeat/endrepeat interpreter with bracket matching + guard; UI step types)
- [x] 37. **P2** wait-until-variable / wait-for-event with timeout — *event-driven sequencing.* (new `wait_until` action step: blocks (delay(10)-yield loop, watchdog-safe) until `variable op value` holds or `delay_ms` timeout — 0 = 30s safety cap; timeout fails the step and aborts the action. Consistent with the engine's existing blocking `delay` step. UI: condition + timeout fields.)
- [x] 38. **P2** Debounce / throttle / cooldown trigger modifiers — *stop runaway/chattering triggers.* (per-input-trigger `debounce_ms` (trailing-edge: fires once the input is quiet for the window, coalescing a burst — evaluated in loop()) and `cooldown_ms` (leading-edge throttle: drops events within the window after a fire). Runtime state per action, reset on re-register; (de)serialized in actions.json + REST; Action Manager fields shown for input triggers.)
- [x] 39. **P2** Scenes — snapshot + restore many outputs/vars at once — *one-tap "movie mode" style presets.* (new `SceneManager` (read-modify-write /scenes.json, ≤32): captures live output states (via OutputManager.fillStateJson) + all variable values; restore re-applies via OutputManager.applyStateJson + typed variable setters. REST GET/capture/restore/DELETE (child routes before parent per the route-matching gotcha); `scene_restore` action step (templated); Action Manager Scenes card (capture/restore/delete) + step picker.)
- [x] 40. **P1** Action templating with variable interpolation `{{node/x/temp}}` — *dynamic, reusable actions.*
- [x] 41. **P2** Per-action enable toggle + last-run time + pass/fail — *easier debugging & control.* (enabled flag was already enforced + a global pass/fail history existed; added an inline per-action last-run indicator (✓/✗ + time, derived from history) and a one-click enable/disable toggle in the action list (upsert-by-id, no editor round-trip).)
- [ ] 42. **P3** Dry-run / simulate mode for actions — *test before it drives hardware.*
- [ ] 43. **P2** Macro arguments/parameters — *one macro, many uses.*
- [ ] 44. **P2** Cross-node automation ack/retry **(roadmap — open)** — *reliable distributed triggers.*

## Web UI

- [ ] 45. **P2** Vue port of the CMS dashboard **(roadmap)** — *one component set across device + fleet.*
- [x] 46. **P3** Dark/light theme toggle (persisted) — *user comfort.* (app-bar sun/moon toggle via Vuetify useTheme; persisted to localStorage, initial theme follows saved choice then OS `prefers-color-scheme` (dark default). Fixed a hardcoded `text-white` title that would vanish in light mode.)
- [x] 47. **P1** Live log console over WebSocket with filter/search — *debug without a serial cable.*
- [ ] 48. **P2** Drag-and-drop dashboard widgets with saved layouts — *personalized monitoring.*
- [x] 49. **P1** Real-time sensor charts (in-browser ring buffer) — *see trends at a glance.*
- [ ] 50. **P2** Mobile-responsive / PWA install — *use the UI as a phone app.*
- [ ] 51. **P3** i18n scaffolding for the frontend — *multi-language reach.*
- [x] 52. **P1** Guided first-run setup wizard — *smooth out-of-box onboarding.* (4-step stepper: name → WiFi → MQTT → review; /setup route, AP-mode dashboard CTA)
- [ ] 53. **P2** JSON schema validation + diff preview in the FS editor — *prevent broken config saves.*
- [ ] 54. **P3** Keyboard shortcuts + command palette — *power-user speed.*
- [ ] 55. **P2** Accessibility pass (ARIA, focus, contrast) — *usable by everyone.*

## CMS / Fleet Management

- [x] 56. **P1** SQLite persistence for historical telemetry **(roadmap — deferred)** — *retain history across CMS restarts.* (already durable via atomic-JSON `JsonStore` — history survives restart; literal SQLite swap intentionally deferred to avoid a native dep that breaks the Node-20 CI. ⚑ revisit if retention/query needs grow)
- [ ] 57. **P2** Richer drill-in: screen listing + live preview **(roadmap — open)** — *see what a device is actually showing.*
- [~] 58. **P1** User accounts + roles (viewer/operator/admin) — *safe multi-user operation.* (role-tiered tokens shipped: admin = full, `CMS_VIEWER_TOKEN` = read-only via method gating; +4 tests. ⚑ full username/password accounts deferred — a security-model decision)
- [x] 59. **P1** Audit log of all pushed commands — *accountability and forensics.* (durable AuditLog records role+action+target+result for single/bulk/template/firmware pushes; GET /api/audit; +3 tests)
- [x] 60. **P1** Staged/canary OTA with auto-halt on failure rate — *don't brick the whole fleet at once.* (deploy `canary` N first; rest proceed only if canary ack-rate ≥ `minAckRate`, else halts; UI canary field. ⚑ ack-based; flash-success/version-change gating is a telemetry follow-up)
- [ ] 61. **P1** Signed firmware + on-device verification **(roadmap — security)** — *closes the fleet-RCE risk.* ⚑ SKIPPED (needs human decision): requires a signing key-custody policy (where the private key lives, algorithm — e.g. ESP32 Secure Boot vs app-level Ed25519) and an embedded public key, plus on-hardware verification. A CMS-served hash would be integrity-only, not authenticity — security theater against the spoofed-host threat. Needs your call before implementing.
- [x] 62. **P2** Config diff viewer before `apply_config` — *know exactly what will change.* (device page "Preview changes" computes a leaf-level JSON diff (added/removed/changed, colour-coded) of the config about to be pushed vs the last config pushed to that device from this browser (localStorage baseline), then requires an explicit "Confirm push & reboot". All diff values HTML-escaped. ⚑ baseline is per-browser, not the device's live config — a CMS-proxied read of the device's /api/config would make it authoritative (follow-up).)
- [ ] 63. **P2** Scheduled fleet jobs (nightly reboot, config reconcile) — *hands-off maintenance.*
- [x] 64. **P3** Per-device notes/metadata + searchable grid — *operational context.* (Device gains free-text `notes` (≤2000 chars); registry.setNotes + gated PUT /api/devices/:id/notes (+1 test). Device page has a Notes editor (doesn't clobber while typing); fleet grid gains a free-text search box matching name/id/board/ip/tags/notes with a "N hidden" count.)
- [ ] 65. **P2** Grafana/Prometheus exporter for fleet metrics — *pro-grade dashboards & alerting.*
- [ ] 66. **P3** Multi-tenant / multi-site grouping — *manage several locations.*

## Mesh / Multi-node

- [ ] 67. **P2** painlessMesh backend behind `INodeLinkBackend` **(roadmap)** — *self-healing larger networks.*
- [ ] 68. **P2** Coordinator/gateway failover & election **(roadmap)** — *no single point of failure.*
- [ ] 69. **P3** Per-link RSSI/quality in topology map **(roadmap)** — *spot weak mesh links.*
- [ ] 70. **P3** Remote display mirroring / live preview of a leaf **(roadmap)** — *monitor headless leaves.*
- [x] 71. **P1** NodeLink ack + retry + sequence dedup — *reliable mesh messaging.* (reliable unicast: per-msg Ack, retransmit up to 3× @300ms, per-source seq dedup with reboot-safe exact-match; broadcasts unaffected)
- [ ] 72. **P2** Mesh-wide variable namespace browser in the UI — *discover what data exists.*
- [ ] 73. **P2** Auto-channel negotiation gateway↔leaves — *resolves the open WiFi/ESP-NOW channel question.*
- [ ] 74. **P3** Over-mesh OTA for infra-less leaves (relayed) — *update nodes with no WiFi.*

## Security & Auth

- [~] 75. **P1** Per-device API token / auth on REST + WS — *device endpoints aren't open by default.* (opt-in `device.api_token`: gates all REST writes, uploads, deletes, the secret-bearing /api/config read, AND the whole FS browser read surface (download/list/stat/selftest). Header-only Bearer (no query-string token — avoids log/Referer leaks), constant-time compare. Frontend sends the token when set, downloads via header-auth blob, Settings field mirrors it to the browser. Hardened per security review (closed a HIGH fs-download read-bypass + a MEDIUM token-in-URL/timing finding). ⚑ remaining: live WebSocket + read-only telemetry GETs (status/sensors/logs — no secrets) not yet gated; the AsyncWebSocket connect event doesn't expose the upgrade token in this fork — small follow-up.)
- [ ] 76. **P2** HTTPS on the device web server — *encrypted local UI/API.*
- [ ] 77. **P2** CSRF + same-origin checks on state-changing endpoints — *block cross-site abuse.*
- [ ] 78. **P1** Secrets in encrypted NVS, not plaintext LittleFS — *protect WiFi/MQTT credentials.* ⚑ SKIPPED (needs hardware decision): real NVS encryption requires ESP32 **flash encryption** (irreversible eFuses — see #79) and ESP8266 has no encrypted key store, so plain-NVS would still be plaintext-on-flash. The *web* exposure (FS browser reading /config.json) is already mitigated by the #75 auth gating; the residual physical-flash-dump risk needs the flash-encryption fuse decision. Your call.
- [ ] 79. **P3** ESP32 flash encryption + secure boot build profile + docs — *hardware-rooted trust.*
- [ ] 80. **P2** Rate-limit/lockout on captive portal + login — *resist brute force.*
- [ ] 81. **P2** CMS HTTPS-behind-proxy + broker auth reference **(roadmap follow-up)** — *safe wider exposure.*
- [x] 82. **P1** CVE/dependency scanning in CI (frontend + CMS) — *catch vulnerable packages early.* (Dependabot + non-blocking `npm audit` job)

## Observability & Diagnostics

- [ ] 83. **P2** Structured remote logging to CMS/syslog — *centralized fleet logs.*
- [x] 84. **P2** On-device `/metrics` in Prometheus format — *standard scraping.* (GET /metrics, text/plain v0.0.4: uptime, free/min/largest heap, heap-fragmentation %, CPU load, boot count, safe-mode, wifi up/RSSI, mqtt up — each labelled `device="<id>"`. Left open like other read-only status endpoints so scrapers need no token.)
- [x] 85. **P1** Enhanced "identify" — blink LED + flash screen + buzzer — *physically locate a device fast.*
- [ ] 86. **P3** Network diagnostics page (ping, MQTT latency, DNS, NTP) — *troubleshoot connectivity in-UI.*
- [x] 87. **P2** Heap fragmentation tracking + leak warning — *catch slow memory leaks.* (HealthMonitor samples free heap + largest contiguous block once per 5s window → session min-free watermark + fragmentation % (`of_largest_free_block` added for both ESP32/ESP8266); logs a single LOG_W when free<20KB or frag≥60%, re-arming on recovery. Surfaced in /api/status (largestFreeBlock, heapFragPercent) and /metrics [[#84]].)
- [ ] 88. **P3** Event-bus inspector (live events) — *debug automations visually.*

## Developer Experience / CI / Testing

- [x] 89. **P1** CI: build all 3 ESP targets + frontend + CMS tests on every PR — *no more "works on my machine".*
- [ ] 90. **P1** Native/host unit tests (Action engine, config merge) via PlatformIO native env — *fast logic tests, no board.* ⚑ BLOCKED (toolchain): no host C++ compiler (g++) in this build environment, so a `platform=native` test env can't compile/verify here. Deferred to avoid adding an unverifiable env that could destabilize the real ESP builds. (Note: the CMS already has a 56-test `node:test` host suite — see #89.) Needs a host with build-essential/g++ to implement+verify.
- [ ] 91. **P2** Frontend unit/component (Vitest) + E2E (Playwright) tests — *UI regressions caught automatically.*
- [ ] 92. **P2** Pre-commit hooks: clang-format + eslint/prettier — *consistent style everywhere.*
- [~] 93. **P1** Device simulator/emulator — *develop UI + CMS without hardware.* (CMS fleet simulator `cms/tools/simulate.ts` via `npm run simulate` — N fake devices publish presence + drifting heartbeats over MQTT and ack commands, so the dashboard/topology/history/alerts/bulk UI work with no hardware. ⚑ a mock device-API server for the Vue app's REST/WS is a follow-up.)
- [ ] 94. **P2** Automated LittleFS image build + size-budget check in CI — *catch oversized assets before flash.*
- [ ] 95. **P2** Release automation: tag → artifacts → GitHub release (read by update check) — *one-click releases.*
- [ ] 96. **P3** Hardware-in-the-loop smoke test per release — *verify on real silicon.*

## Documentation & Community

- [ ] 97. **P2** Hardware wiring diagrams + supported-parts matrix — *lower the barrier to building.*
- [x] 98. **P1** End-to-end "your first automation" tutorial — *fast time-to-value for new users.* (docs/first-automation-tutorial.md: power-on → wizard → define LED+button → button-toggles-LED action + notification → schedule/other triggers → troubleshooting; linked from README. Uses real shipped features.)
- [ ] 99. **P2** Plugin authoring guide with a worked example — *grow the plugin ecosystem.*
- [ ] 100. **P1** Choose and add a LICENSE (currently TBD) — *unblocks adoption & contribution.*

---

_Generated as a planning backlog — re-prioritize freely. Update checkboxes as items land._
