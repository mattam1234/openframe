# OpenFrame

> A modular hardware automation and control platform with ESP firmware and a browser-based UI.

OpenFrame lets you wire up inputs, outputs, sensors, displays, touch interactions, and expansion modules, then configure and monitor the device from a web interface without reflashing for every change.

---

## Project Status

OpenFrame has completed the planned MVP implementation through phases 0–10.

- ✅ ESP32, ESP32-S3, and ESP8266 firmware targets
- ✅ Core runtime: logging, event bus, storage, configuration, variables
- ✅ Connectivity: WiFi captive portal, MQTT, Home Assistant, OTA
- ✅ Hardware layer: inputs, outputs, sensors, displays, touch, I2C modules
- ✅ Automation: action engine, conditions, macros, notifications
- ✅ Device API: REST endpoints, WebSocket updates, LittleFS-backed config, filesystem browser API
- ✅ Web UI: dashboard (with storage health), layout designer, screen designer, action manager, filesystem browser, settings
- ✅ Profiles, templates, device health monitoring, and plugin architecture foundations

See [`docs/implementation-plan.md`](docs/implementation-plan.md) for the detailed completion checklist.

---

## Implemented Features

### Firmware

- PlatformIO firmware for `esp32dev`, `esp32s3dev`, and `esp8266dev`
- JSON-backed configuration and variable persistence with LittleFS
- WiFi first-boot AP + captive portal with reconnect and fallback logic
- MQTT integration and Home Assistant MQTT discovery/service handling
- OTA upload support and GitHub release check endpoint
- Action engine with delays, HTTP, MQTT, variable, Home Assistant, page, and notification actions
- Macro execution and action history tracking
- Health monitoring for heap, PSRAM, CPU, WiFi RSSI, reboot reason, and uptime
- Notification delivery to displays and the web UI

### Hardware Support

- Inputs: buttons, toggles, encoders, analog controls
- Outputs: LEDs, PWM, RGB, WS2812, relays, buzzers
- Sensors: BME280, BMP280, DHT22, DS18B20, SHT31, BH1750, INA219, MPU6050
- Displays: SSD1306, SH1106, ST7789, ILI9341, Nextion
- Touch routing, gestures, page navigation, and widget interaction
- I2C module discovery and module handler registry

### Web UI

- Dashboard for live device status, health, and LittleFS storage usage
- Device Layout Designer
- Screen Designer
- Sensor Dashboard
- Action Manager and macro editor
- Module Manager
- Home Assistant Manager
- Logs Viewer
- Filesystem Browser — browse, inspect/edit, create, upload, download, rename, and delete files stored on the device (LittleFS)
- Settings, profile management, and template import/export

---

## Repository Layout

```text
openframe/
├── firmware/               # PlatformIO firmware and LittleFS assets
│   ├── platformio.ini
│   ├── data/               # Built frontend assets served by the device
│   └── src/
│       ├── api/
│       ├── core/
│       ├── hardware/
│       ├── managers/
│       └── plugin/
├── frontend/               # Vue 3 + Vite + Pinia + Vuetify UI
│   ├── package.json
│   └── src/
└── docs/
    ├── implementation-plan.md
    ├── plugin-architecture.md
    └── product-specifications.md
```

---

## Build and Run

### Firmware

```bash
cd firmware
python -m platformio run -e esp32dev
python -m platformio run -e esp32s3dev
python -m platformio run -e esp8266dev
```

To upload instead of only building:

```bash
cd firmware
python -m platformio run -e esp32dev -t upload
```

### Frontend

For local development:

```bash
cd frontend
npm install
npm run dev
```

For a production build served from the device:

```bash
cd frontend
npm install
npm run build
```

The frontend build outputs to `firmware/data/www`, which is served by the firmware from LittleFS. The dev server proxies `/api` and `/ws` to the device IP configured in `frontend/.env`.

### First Boot

On first boot the device creates an `OpenFrame-XXXX` access point. Connect to it and open `http://192.168.4.1` to configure WiFi and device settings.

---

## Documentation

- [Product Specifications](docs/product-specifications.md)
- [Implementation Plan](docs/implementation-plan.md)
- [Plugin Architecture](docs/plugin-architecture.md)

---

## Technology Stack

| Layer | Technology |
|---|---|
| Firmware targets | ESP32, ESP32-S3, ESP8266 |
| Firmware framework | Arduino via PlatformIO |
| Firmware services | Async WebServer, ElegantOTA, ArduinoJson, PubSubClient |
| Storage | LittleFS + JSON |
| Frontend | Vue 3, Vite, Pinia, Vuetify |
| Communication | REST API, WebSocket, MQTT |

---

## License

TBD
