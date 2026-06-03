# OpenFrame — Implementation Plan

This document breaks the product specification into concrete, phased implementation steps targeting the v1.0 MVP.

See [product-specifications.md](product-specifications.md) for full requirements.

---

## Phase 0 — Project Scaffolding

Set up the project structure so every subsequent phase has a clean foundation to build on.

- [x] Create PlatformIO project (`firmware/`) with `esp32dev` and `esp32s3dev` environments
- [x] Add core library dependencies to `platformio.ini`:
  - ArduinoJson
  - AsyncTCP + ESPAsyncWebServer
  - ElegantOTA
  - LittleFS
- [x] Create `main.cpp` entry point that initialises all subsystem managers
- [x] Create Vue 3 + Vite + Pinia + Vuetify frontend scaffold (`frontend/`)
- [x] Add `.gitignore` for PlatformIO build artefacts and node_modules
- [x] Add `docs/` directory with `product-specifications.md` and this file

---

## Phase 1 — Core System (Foundation)

All other modules depend on these subsystems.

### Logger

- [x] `Logger` class with log levels: Trace, Debug, Info, Warning, Error, Fatal
- [x] Output to Serial
- [x] Ring buffer storing latest 1000 entries
- [ ] Expose log entries over WebSocket for web UI (Phase 5)

### Event Bus

- [x] Publish/subscribe event bus (`EventBus`)
- [x] Typed event definitions (e.g. `InputEvent`, `SensorEvent`, `ActionEvent`)

### Storage Manager

- [x] `StorageManager` wrapping LittleFS
- [x] `readJson()` / `writeJson()` helpers
- [x] Default config file creation on first boot

### Configuration Manager

- [x] `ConfigManager` that loads/saves device settings from LittleFS JSON
- [x] Schema: WiFi, MQTT, HA, device name, board type

### Variable Manager

- [x] Global `VariableManager` supporting Integer, Float, Boolean, String variables
- [x] `get()`, `set()`, `subscribe()` API
- [x] Persist variables to LittleFS

---

## Phase 2 — Connectivity

### WiFi Manager

- [x] First-boot AP + captive portal (SSID: `OpenFrame-XXXX`)
- [x] Connect to stored credentials on subsequent boots
- [x] Retry logic with AP fallback on failure
- [x] Broadcast `WifiConnected` / `WifiDisconnected` events

### MQTT Manager

- [x] Connect to configured broker
- [x] `publish()` / `subscribe()` helpers
- [x] Reconnect with back-off
- [x] Route incoming messages to Event Bus

### Home Assistant Manager

- [x] MQTT Discovery: auto-publish discovery payloads for all entities
- [x] Supported entity types: sensor, binary_sensor, button, switch, select, number, text
- [x] Receive HA commands and emit internal events

### OTA Manager

- [x] Local firmware upload via ESPAsyncWebServer + ElegantOTA
- [x] GitHub release check endpoint
- [x] Progress events to WebSocket
- [x] Rollback support via partition scheme

---

## Phase 3 — Hardware Layer

### Input Manager

- [x] Digital input driver (buttons, toggles, keypads, encoder buttons)
  - Events: Press, Release, Hold, Long Press, Double Press, Triple Press, Repeat
- [x] Analog input driver (potentiometers, sliders, LDR, joystick)
  - Events: Value Changed, Threshold Reached, Range Entered / Exited
- [x] Input config loaded from LittleFS JSON
- [x] Inputs publish events to Event Bus

### Output Manager

- [x] LED driver (digital on/off, PWM brightness)
- [x] RGB LED driver
- [x] WS2812 driver (via FastLED or NeoPixel)
- [x] Relay driver
- [x] Buzzer driver (tone + beep patterns)
- [x] Output config loaded from LittleFS JSON

### Sensor Manager

- [x] Extensible sensor registry (`registerSensor()`)
- [ ] Driver implementations:
  - [x] BME280
  - [ ] BMP280
  - [ ] DHT22
  - [ ] DS18B20
  - [ ] SHT31
  - [ ] BH1750
  - [ ] INA219
  - [ ] MPU6050
- [x] Polling loop with configurable intervals
- [x] Sensor values published to VariableManager and Event Bus

### Display Manager

- [x] Abstract `DisplayProvider` interface
- [x] OLED driver (SSD1306 via Adafruit)
- [ ] Additional OLED variants (SH1106, SSD1327)
- [ ] TFT driver (ST7789, ILI9341, ILI9488) via TFT_eSPI
- [ ] Nextion smart display driver:
  - [ ] UART communication
  - [ ] Page synchronisation
  - [ ] Component binding to variables
  - [ ] Touch event forwarding to Event Bus
  - [ ] Live component updates
  - Primary target: NX4827T043_011
- [x] Display pages and widgets loaded from LittleFS JSON (no hardcoded layouts)

### Touch Manager

- [ ] Touch input routing from display touch events
- [ ] Widget hit-testing (button, slider)
- [ ] Page navigation
- [ ] Gesture detection (swipe)
- [ ] Touch events → Action Engine

### Module Manager

- [ ] I2C bus scan for expansion modules
- [ ] Module registry with auto-discovery
- [ ] Module type handlers: buttons, potentiometers, encoders, displays, sensors, relays, LEDs
- [ ] Hot-plug detection

---

## Phase 4 — Action Engine & Macro System

- [ ] `ActionEngine` with action type registry (`registerAction()`)
- [ ] Built-in action types:
  - [ ] Delay
  - [ ] HTTP request
  - [ ] MQTT publish
  - [ ] Variable set / increment / toggle
  - [ ] Home Assistant service call
  - [ ] Page change
  - [ ] Notification
  - [ ] Keyboard shortcut (ESP32-S3 USB HID — stub for now)
  - [ ] Media control (stub)
- [ ] Condition evaluation (variable comparisons, sensor thresholds)
- [ ] Action chaining
- [ ] `MacroManager` — named sequences of actions
- [ ] Actions and macros stored in LittleFS JSON
- [ ] Action history ring buffer (1000 entries)

---

## Phase 5 — REST & WebSocket API

- [ ] `ApiServer` on ESPAsyncWebServer
- [ ] REST endpoints:
  - `GET /api/status` — device health
  - `GET/POST /api/config` — configuration
  - `GET/POST /api/variables`
  - `GET/POST /api/inputs`
  - `GET/POST /api/outputs`
  - `GET/POST /api/sensors`
  - `GET/POST /api/displays`
  - `GET/POST /api/actions`
  - `GET/POST /api/macros`
  - `GET/POST /api/profiles`
  - `GET/POST /api/templates`
  - `GET/POST /api/modules`
  - `GET /api/logs`
  - `POST /api/ota/upload`
  - `GET /api/ota/check`
- [ ] WebSocket endpoint `/ws`
  - Broadcast: sensor values, variable changes, log entries, device health
  - Receive: action triggers, config changes, page navigation

---

## Phase 6 — Web UI

### Scaffold

- [x] Vue 3 + Vite project with Vuetify and Pinia
- [x] Router setup for all views
- [x] WebSocket store (Pinia) receiving live data
- [x] REST API client module

### Views

- [ ] **Dashboard** — device status, memory, CPU, uptime, WiFi, firmware version
- [ ] **Device Layout Designer** — drag-and-drop hardware editor, real-time state
- [ ] **Screen Designer** — visual display page editor, widget binding to variables/sensors
- [ ] **Sensor Dashboard** — live values, historical graphs, min/max, export
- [ ] **Action Manager** — create/edit actions, macros, conditions
- [ ] **Module Manager** — discovered modules, diagnostics
- [ ] **Home Assistant Manager** — entity mapping, discovery toggle
- [ ] **Logs Viewer** — debug log stream, action history
- [ ] **Settings** — WiFi, MQTT, OTA, system

---

## Phase 7 — Profiles & Templates

- [ ] **Profile Manager** — create, switch, and delete profiles
  - Each profile stores: layout, display pages, actions, variables, HA mappings
  - Switching profiles does not require reflash
- [ ] **Template Manager** — export / import complete device templates as JSON
  - Templates include everything a profile contains plus hardware config

---

## Phase 8 — Device Health Monitoring

- [ ] Periodic health task reporting:
  - Heap usage, PSRAM usage
  - CPU load (idle task measurement)
  - WiFi RSSI
  - I2C error counters
  - Sensor failure flags
  - Reboot reason
  - Uptime
- [ ] Health data pushed over WebSocket to Dashboard

---

## Phase 9 — Notification System

- [ ] `NotificationManager` with named notification types
- [ ] Render notifications on connected displays
- [ ] Push notification events over WebSocket to web UI
- [ ] Notification types: firmware update, sensor disconnect, WiFi disconnect, HA disconnect, MQTT disconnect

---

## Phase 10 — Plugin Architecture (Foundation)

- [ ] Define registration interfaces for each extension point:
  - `SensorRegistry::registerDriver()`
  - `DisplayRegistry::registerProvider()`
  - `ActionRegistry::registerAction()`
  - `ModuleRegistry::registerModule()`
  - `ProtocolRegistry::registerProtocol()`
- [ ] Document plugin interface contracts
- [ ] Ensure all built-in implementations use the same registration path

---

## v1.0 MVP Checklist

Cross-reference with [product-specifications.md § Version 1.0](product-specifications.md#version-10-minimum-viable-product).

| Feature | Phase | Status |
|---|---|---|
| ESP32 support | 0 | ✅ |
| ESP32-S3 support | 0 | ✅ |
| WiFi + captive portal | 2 | ✅ |
| OTA updates | 2 | ✅ |
| SSD1306 display | 3 | ⬜ |
| Nextion NX4827T043_011 | 3 | ⬜ |
| Buttons | 3 | ✅ |
| Potentiometers | 3 | ✅ |
| LDR sensors | 3 | ⬜ |
| Variable system | 1 | ✅ |
| Action engine | 4 | ⬜ |
| Conditional actions | 4 | ⬜ |
| Macro support | 4 | ⬜ |
| MQTT integration | 2 | ✅ |
| Home Assistant MQTT Discovery | 2 | ✅ |
| Device Layout Designer | 6 | ⬜ |
| Screen Designer | 6 | ⬜ |
| Sensor Dashboard | 6 | ⬜ |
| Action History | 4 | ⬜ |
| Debug Logging | 1 | ✅ |
| I2C Module Support | 3 | ⬜ |
| Device Templates | 7 | ⬜ |
| Device Health Monitoring | 8 | ⬜ |
| WebSocket live updates | 5 | ⬜ |
| JSON config storage | 1 | ✅ |
| LittleFS storage | 1 | ✅ |
