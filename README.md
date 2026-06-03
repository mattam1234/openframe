# OpenFrame

> A modular ESP32-based hardware automation and control platform.

OpenFrame lets you connect sensors, buttons, displays, touchscreens, and expansion modules and configure everything through a browser — no firmware reflash required.

---

## Features

- **ESP32 / ESP32-S3** support via PlatformIO + Arduino Framework
- **Visual web UI** — Device Layout Designer, Screen Designer, Sensor Dashboard
- **Action Engine** — chainable, conditional actions and macros
- **Variable System** — global integer / float / boolean / string variables
- **Display Support** — SSD1306, SH1106, TFT (ST7789, ILI9341, ILI9488), Nextion smart displays
- **Sensor Support** — BME280, DHT22, DS18B20, BH1750, INA219, MPU6050 and more
- **Home Assistant** — MQTT Discovery, entity binding, service calls
- **MQTT** — publish, subscribe, variable sync
- **OTA Updates** — local upload, remote GitHub release checks, rollback
- **WiFi** — captive-portal first-boot setup, AP fallback
- **LittleFS** storage — JSON configuration, profiles, templates, action history
- **I2C Module System** — auto-discovery of expansion modules
- **USB HID** — keyboard / media control on ESP32-S3 (future)

---

## Repository Layout

```
openframe/
├── firmware/               # PlatformIO ESP32 firmware
│   ├── platformio.ini
│   └── src/
│       ├── main.cpp
│       ├── core/           # Logger, EventBus, ConfigManager, StorageManager
│       ├── managers/       # WiFi, MQTT, OTA, HA, Variables
│       ├── hardware/       # Input, Output, Sensor, Display, Touch, Module
│       └── api/            # REST + WebSocket handlers
├── frontend/               # Vue 3 + Pinia + Vuetify web UI
│   ├── package.json
│   └── src/
│       ├── views/          # Dashboard, LayoutDesigner, ScreenDesigner, etc.
│       ├── components/
│       ├── stores/         # Pinia stores
│       └── api/            # WebSocket + REST client
└── docs/
    ├── product-specifications.md
    └── implementation-plan.md
```

---

## Getting Started

### Firmware

1. Install [PlatformIO](https://platformio.org/).
2. Open the `firmware/` folder in VS Code with the PlatformIO extension.
3. Select your target environment (`esp32dev` or `esp32s3dev`).
4. Build and upload:

```bash
cd firmware
pio run -t upload
```

### Frontend (development)

```bash
cd frontend
npm install
npm run dev
```

The dev server proxies API calls to the device IP configured in `frontend/.env`.

### First Boot

On first boot the device starts an Access Point named `OpenFrame-XXXX`.  
Connect to it and open `http://192.168.4.1` to configure WiFi and basic settings.

---

## Documentation

- [Product Specifications](docs/product-specifications.md)
- [Implementation Plan](docs/implementation-plan.md)

---

## Technology Stack

| Layer | Technology |
|---|---|
| Firmware MCU | ESP32 / ESP32-S3 |
| Firmware Framework | Arduino + FreeRTOS via PlatformIO |
| Firmware Libraries | ArduinoJson, AsyncTCP, ESPAsyncWebServer, ElegantOTA |
| Storage | LittleFS + JSON |
| Frontend | Vue 3, Vite, Pinia, Vuetify |
| Communication | REST API, WebSocket, MQTT |

---

## Contributing

See the [implementation plan](docs/implementation-plan.md) for the current development roadmap and open tasks.

---

## License

TBD
