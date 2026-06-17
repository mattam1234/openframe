# OpenFrame — Feature Backlog (100 items)

A working backlog of features worth integrating into OpenFrame, grouped by area.
Items already tracked as open/in-progress in
[`docs/fleet-and-mesh-roadmap.md`](docs/fleet-and-mesh-roadmap.md) are marked **(roadmap)**.

Legend: `- [ ]` not started · `- [~]` partial · `- [x]` done · **P1** high value / foundational · **P2** solid · **P3** nice-to-have / larger effort

---

## Firmware & Core Runtime

- [x] 1. **P1** Config schema versioning + auto-migration on boot — *survive old LittleFS JSON formats without bricking saved configs.*
- [x] 2. **P1** Atomic config writes (write-temp-then-rename) — *no more corrupt config on power loss mid-save.*
- [ ] 3. **P2** Config backup/restore slots with one-tap rollback — *undo a bad config push instantly.*
- [ ] 4. **P2** Factory-reset long-press that wipes config but keeps WiFi — *recover a misconfigured device in the field.*
- [x] 5. **P1** Watchdog + safe-mode boot after repeated crash loops — *device stays reachable instead of dying silently.*
- [ ] 6. **P2** Crash/panic backtrace stored to LittleFS, surfaced in UI — *diagnose resets without a serial cable.*
- [ ] 7. **P3** Deep-sleep / light-sleep power profiles — *unlocks battery-powered nodes.*
- [ ] 8. **P2** Brownout detection + state persist before reset — *don't lose state on flaky power.*
- [ ] 9. **P3** Boot-time self-test (RAM, flash, I2C scan) with report — *catch bad hardware early.*
- [x] 10. **P1** Local cron/scheduler engine for time-based actions — *automations without cluster/NTP dependency.* (interval = no clock needed; daily = needs NTP)
- [ ] 11. **P2** Typed variables (enums, ranges, units, persist flags) — *richer, validated automation data.*
- [ ] 12. **P3** RTC (DS3231) support — *accurate time with no WiFi/NTP.*

## Hardware Support

- [ ] 13. **P2** Stepper driver support (A4988/DRV8825/TMC2209) — *motion projects.*
- [ ] 14. **P1** Servo output type with angle/sweep actions — *common, high-demand actuator.*
- [ ] 15. **P2** Encoder-with-button composite input — *menu navigation in one control.*
- [ ] 16. **P3** ESP32 native capacitive touch pins as input — *touch buttons with no extra hardware.*
- [ ] 17. **P2** More sensors: SCD40/41, SGP30, VL53L0X, MAX6675 — *air quality, ToF, thermocouple coverage.*
- [ ] 18. **P3** Load cell + HX711 (weight/force) — *scales and force sensing.*
- [ ] 19. **P1** Addressable-LED effect presets (rainbow, chase, breathe, fire) — *instant polish for WS2812 users.*
- [ ] 20. **P3** E-paper (Waveshare SPI) display driver — *low-power info panels.*
- [ ] 21. **P3** Matrix keypad input driver — *keypad entry / PIN pads.*
- [ ] 22. **P2** Generic I2C/SPI register-map plugin (add chips via JSON) — *new sensors with no reflash.*
- [ ] 23. **P3** PWM gamma-correction curves for LED/RGB — *visually linear dimming.*

## Connectivity & Protocols

- [x] 24. **P1** mDNS so devices are `openframe-xxxx.local` — *find devices without hunting IPs.*
- [ ] 25. **P2** Static IP / DHCP reservation hints in WiFi settings — *stable addressing for the CMS.*
- [ ] 26. **P2** Multi-AP credentials with roaming to strongest network — *resilient connectivity.*
- [ ] 27. **P3** Ethernet (W5500/LAN8720) support — *wired, reliable nodes.*
- [ ] 28. **P1** MQTT over TLS with CA cert upload — *secure broker links beyond the LAN.*
- [x] 29. **P1** MQTT reconnect backoff + last-error in UI — *no silent disconnects.*
- [ ] 30. **P3** Matter/Thread bridge investigation — *native smart-home pairing.*
- [ ] 31. **P2** Richer HA entity types (climate, cover, light brightness) — *fuller Home Assistant integration.*
- [x] 32. **P1** Outbound webhook action (POST JSON, templated) — *integrate with anything (Slack, IFTTT, custom).* (HTTP action now interpolates `{{variable}}` in URL + body — see [[#40]])
- [ ] 33. **P3** REST rate limiting + ETag/caching on config — *robustness under load.*
- [ ] 34. **P2** NTP pool config + timezone/DST handling — *correct local time everywhere.*

## Automation Engine

- [ ] 35. **P1** Visual rule builder (trigger → condition → action) in the UI — *automations without hand-editing JSON.*
- [ ] 36. **P1** Action steps: if/else branch + bounded loop — *real conditional logic.*
- [ ] 37. **P2** wait-until-variable / wait-for-event with timeout — *event-driven sequencing.*
- [ ] 38. **P2** Debounce / throttle / cooldown trigger modifiers — *stop runaway/chattering triggers.*
- [ ] 39. **P2** Scenes — snapshot + restore many outputs/vars at once — *one-tap "movie mode" style presets.*
- [x] 40. **P1** Action templating with variable interpolation `{{node/x/temp}}` — *dynamic, reusable actions.*
- [ ] 41. **P2** Per-action enable toggle + last-run time + pass/fail — *easier debugging & control.*
- [ ] 42. **P3** Dry-run / simulate mode for actions — *test before it drives hardware.*
- [ ] 43. **P2** Macro arguments/parameters — *one macro, many uses.*
- [ ] 44. **P2** Cross-node automation ack/retry **(roadmap — open)** — *reliable distributed triggers.*

## Web UI

- [ ] 45. **P2** Vue port of the CMS dashboard **(roadmap)** — *one component set across device + fleet.*
- [ ] 46. **P3** Dark/light theme toggle (persisted) — *user comfort.*
- [x] 47. **P1** Live log console over WebSocket with filter/search — *debug without a serial cable.*
- [ ] 48. **P2** Drag-and-drop dashboard widgets with saved layouts — *personalized monitoring.*
- [ ] 49. **P1** Real-time sensor charts (in-browser ring buffer) — *see trends at a glance.*
- [ ] 50. **P2** Mobile-responsive / PWA install — *use the UI as a phone app.*
- [ ] 51. **P3** i18n scaffolding for the frontend — *multi-language reach.*
- [ ] 52. **P1** Guided first-run setup wizard — *smooth out-of-box onboarding.*
- [ ] 53. **P2** JSON schema validation + diff preview in the FS editor — *prevent broken config saves.*
- [ ] 54. **P3** Keyboard shortcuts + command palette — *power-user speed.*
- [ ] 55. **P2** Accessibility pass (ARIA, focus, contrast) — *usable by everyone.*

## CMS / Fleet Management

- [ ] 56. **P1** SQLite persistence for historical telemetry **(roadmap — deferred)** — *retain history across CMS restarts.*
- [ ] 57. **P2** Richer drill-in: screen listing + live preview **(roadmap — open)** — *see what a device is actually showing.*
- [ ] 58. **P1** User accounts + roles (viewer/operator/admin) — *safe multi-user operation.*
- [ ] 59. **P1** Audit log of all pushed commands — *accountability and forensics.*
- [ ] 60. **P1** Staged/canary OTA with auto-halt on failure rate — *don't brick the whole fleet at once.*
- [ ] 61. **P1** Signed firmware + on-device verification **(roadmap — security)** — *closes the fleet-RCE risk.*
- [ ] 62. **P2** Config diff viewer before `apply_config` — *know exactly what will change.*
- [ ] 63. **P2** Scheduled fleet jobs (nightly reboot, config reconcile) — *hands-off maintenance.*
- [ ] 64. **P3** Per-device notes/metadata + searchable grid — *operational context.*
- [ ] 65. **P2** Grafana/Prometheus exporter for fleet metrics — *pro-grade dashboards & alerting.*
- [ ] 66. **P3** Multi-tenant / multi-site grouping — *manage several locations.*

## Mesh / Multi-node

- [ ] 67. **P2** painlessMesh backend behind `INodeLinkBackend` **(roadmap)** — *self-healing larger networks.*
- [ ] 68. **P2** Coordinator/gateway failover & election **(roadmap)** — *no single point of failure.*
- [ ] 69. **P3** Per-link RSSI/quality in topology map **(roadmap)** — *spot weak mesh links.*
- [ ] 70. **P3** Remote display mirroring / live preview of a leaf **(roadmap)** — *monitor headless leaves.*
- [ ] 71. **P1** NodeLink ack + retry + sequence dedup — *reliable mesh messaging.*
- [ ] 72. **P2** Mesh-wide variable namespace browser in the UI — *discover what data exists.*
- [ ] 73. **P2** Auto-channel negotiation gateway↔leaves — *resolves the open WiFi/ESP-NOW channel question.*
- [ ] 74. **P3** Over-mesh OTA for infra-less leaves (relayed) — *update nodes with no WiFi.*

## Security & Auth

- [ ] 75. **P1** Per-device API token / auth on REST + WS — *device endpoints aren't open by default.*
- [ ] 76. **P2** HTTPS on the device web server — *encrypted local UI/API.*
- [ ] 77. **P2** CSRF + same-origin checks on state-changing endpoints — *block cross-site abuse.*
- [ ] 78. **P1** Secrets in encrypted NVS, not plaintext LittleFS — *protect WiFi/MQTT credentials.*
- [ ] 79. **P3** ESP32 flash encryption + secure boot build profile + docs — *hardware-rooted trust.*
- [ ] 80. **P2** Rate-limit/lockout on captive portal + login — *resist brute force.*
- [ ] 81. **P2** CMS HTTPS-behind-proxy + broker auth reference **(roadmap follow-up)** — *safe wider exposure.*
- [x] 82. **P1** CVE/dependency scanning in CI (frontend + CMS) — *catch vulnerable packages early.* (Dependabot + non-blocking `npm audit` job)

## Observability & Diagnostics

- [ ] 83. **P2** Structured remote logging to CMS/syslog — *centralized fleet logs.*
- [ ] 84. **P2** On-device `/metrics` in Prometheus format — *standard scraping.*
- [x] 85. **P1** Enhanced "identify" — blink LED + flash screen + buzzer — *physically locate a device fast.*
- [ ] 86. **P3** Network diagnostics page (ping, MQTT latency, DNS, NTP) — *troubleshoot connectivity in-UI.*
- [ ] 87. **P2** Heap fragmentation tracking + leak warning — *catch slow memory leaks.*
- [ ] 88. **P3** Event-bus inspector (live events) — *debug automations visually.*

## Developer Experience / CI / Testing

- [x] 89. **P1** CI: build all 3 ESP targets + frontend + CMS tests on every PR — *no more "works on my machine".*
- [ ] 90. **P1** Native/host unit tests (Action engine, config merge) via PlatformIO native env — *fast logic tests, no board.*
- [ ] 91. **P2** Frontend unit/component (Vitest) + E2E (Playwright) tests — *UI regressions caught automatically.*
- [ ] 92. **P2** Pre-commit hooks: clang-format + eslint/prettier — *consistent style everywhere.*
- [ ] 93. **P1** Device simulator/emulator — *develop UI + CMS without hardware.*
- [ ] 94. **P2** Automated LittleFS image build + size-budget check in CI — *catch oversized assets before flash.*
- [ ] 95. **P2** Release automation: tag → artifacts → GitHub release (read by update check) — *one-click releases.*
- [ ] 96. **P3** Hardware-in-the-loop smoke test per release — *verify on real silicon.*

## Documentation & Community

- [ ] 97. **P2** Hardware wiring diagrams + supported-parts matrix — *lower the barrier to building.*
- [ ] 98. **P1** End-to-end "your first automation" tutorial — *fast time-to-value for new users.*
- [ ] 99. **P2** Plugin authoring guide with a worked example — *grow the plugin ecosystem.*
- [ ] 100. **P1** Choose and add a LICENSE (currently TBD) — *unblocks adoption & contribution.*

---

_Generated as a planning backlog — re-prioritize freely. Update checkboxes as items land._
