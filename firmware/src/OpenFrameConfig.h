#pragma once

// Board detection helpers
#if defined(ESP32S3_BOARD)
    #define OF_BOARD_TYPE "ESP32-S3"
    #define OF_USB_HID_SUPPORTED 1
#elif defined(ESP32C3_BOARD)
    #define OF_BOARD_TYPE "ESP32-C3"
    #define OF_USB_HID_SUPPORTED 0
#elif defined(ESP32_BOARD)
    #define OF_BOARD_TYPE "ESP32"
    #define OF_USB_HID_SUPPORTED 0
#elif defined(ESP8266_BOARD)
    #define OF_BOARD_TYPE "ESP8266"
    #define OF_USB_HID_SUPPORTED 0
#else
    #define OF_BOARD_TYPE "Unknown"
    #define OF_USB_HID_SUPPORTED 0
#endif

// Firmware version
#define OF_VERSION_MAJOR 0
#define OF_VERSION_MINOR 1
#define OF_VERSION_PATCH 0
#define OF_VERSION_STRING "0.1.0"

// Persisted config schema version. Bump when the on-disk config layout changes
// and add a matching step in ConfigManager::migrate(). A missing/0 version means
// a legacy (pre-versioning) config, which is migrated up on first boot.
#define OF_CONFIG_SCHEMA_VERSION 1

// Default AP settings
#define OF_AP_SSID_PREFIX "OpenFrame-"
#define OF_AP_PASSWORD ""
#define OF_AP_IP "192.168.4.1"

// Storage
#define OF_CONFIG_PATH "/config.json"
#define OF_VARIABLES_PATH "/variables.json"
#define OF_INPUTS_PATH "/inputs.json"
#define OF_OUTPUTS_PATH "/outputs.json"
#define OF_SENSORS_PATH "/sensors.json"
#define OF_DISPLAYS_PATH "/displays.json"
#define OF_PROFILES_PATH "/profiles"
#define OF_TEMPLATES_PATH "/templates"
#define OF_DISPLAY_PAGES_PATH "/pages"
#define OF_IMAGES_PATH "/images"
#define OF_ACTIONS_PATH "/actions.json"
#define OF_MACROS_PATH "/macros.json"
#define OF_SCENES_PATH "/scenes.json"
#define OF_LOGS_PATH "/logs.json"
#define OF_NOTIFICATIONS_PATH "/notifications.json"
#define OF_CRASHLOG_PATH "/crashlog.json"
#define OF_MQTT_CA_PATH "/mqtt_ca.pem"
// Set of HA discovery topics published last boot, so we can clear (publish empty
// retained payload) the ones for entities that no longer exist after a config edit.
#define OF_HA_ENTITIES_PATH "/ha_entities.json"
// List of Home Assistant entities imported into the variable bus (HA → OpenFrame):
// each maps an HA entity_id to a live Variable that the display designer / actions
// can read, and (when writable) control. See HaBridgeManager.
#define OF_HA_IMPORT_PATH "/ha_import.json"
#define OF_CONFIG_BACKUP_DIR "/cfgbak"
#define OF_CONFIG_BACKUP_KEEP 5

// ── Resource profile ──────────────────────────────────────────────────────────
// "Constrained" boards have a small heap and/or a single core and cannot afford
// the full set of in-RAM buffers, the live log-over-WebSocket firehose, or every
// optional driver compiled in. The ESP8266 (~80 KB heap, ~25 KB free at boot) and
// the single-core ESP32-C3 (flash ~81% full) both qualify. Everything below is
// `#ifndef`-guarded so platformio.ini build_flags can override per board.
#if defined(ESP8266_BOARD) || defined(ESP32C3_BOARD)
    #define OF_CONSTRAINED 1
#else
    #define OF_CONSTRAINED 0
#endif

// Ring buffer sizes — each LogEntry / action-history record holds Arduino Strings,
// so a large ring is tens of KB of heap. Keep them small where heap is scarce.
#ifndef OF_LOG_BUFFER_SIZE
    #if OF_CONSTRAINED
        #define OF_LOG_BUFFER_SIZE 80
    #elif defined(ESP32_BOARD)
        // Classic ESP32: dual-core but only ~320 KB DRAM, most of which is already
        // claimed by the BLE-HID (NimBLE) + WiFi-AP + async web stack. A full
        // 1000-entry ring of String-bearing LogEntry records is ~70 KB of heap —
        // far too much here. The S3 targets (PSRAM, 512 KB DRAM) keep the full ring.
        #define OF_LOG_BUFFER_SIZE 200
    #else
        #define OF_LOG_BUFFER_SIZE 1000
    #endif
#endif
// Cap on how many recent log lines a WebSocket client receives in its connect
// snapshot. The full ring stays available via the REST /api/logs endpoint; the
// live stream delivers everything after connect. Keep this small — the snapshot
// is built as a JSON array in RAM at connect time, on whatever heap is free then.
#ifndef OF_WS_LOG_SNAPSHOT_MAX
    #define OF_WS_LOG_SNAPSHOT_MAX 40
#endif

// Auto-register a live Variable for every input/output/sensor property (F1), so
// they can be read (and, for outputs, written) like any other variable and bound
// in the screen designer. OFF on constrained boards: dozens of extra String-keyed
// map entries fragment the already-tight heap and bloat the /api/variables + WS
// snapshot payloads. Override with -DOF_ENABLE_HW_VARIABLES=1/0 per board.
#ifndef OF_ENABLE_HW_VARIABLES
    #define OF_ENABLE_HW_VARIABLES (!OF_CONSTRAINED)
#endif
#ifndef OF_ACTION_HISTORY_SIZE
    #if OF_CONSTRAINED
        #define OF_ACTION_HISTORY_SIZE 100
    #else
        #define OF_ACTION_HISTORY_SIZE 1000
    #endif
#endif

// Home Assistant entity import (HA → variable bus). Reads external HA entity state
// into live Variables (bind them in the screen designer / read them in actions) and,
// for writable entities, controls the HA device. The import set is user-curated and
// bounded, so the extra variables are few — enabled on all boards. The MQTT transport
// (HA Statestream + call_service) works everywhere; the richer WebSocket transport
// (turnkey control via the HA API, no HA-side automations) is gated separately below.
#ifndef OF_ENABLE_HA_IMPORT
    #define OF_ENABLE_HA_IMPORT 1
#endif
// WebSocket transport to the HA API (long-lived token): a persistent client socket +
// state_changed event firehose + larger JSON. Limited to the ESP32-S3 family — they
// have PSRAM and the IRAM headroom for the WebSockets client, and they're the boards
// with displays where you'd show HA info. The classic esp32dev is already IRAM-bound
// (BLE-HID + WiFi + async web) and overflows with the WS client added, and the
// constrained boards (C3/8266) lack the heap; all of them keep the MQTT fallback.
#ifndef OF_ENABLE_HA_WS
    #if defined(ESP32S3_BOARD)
        #define OF_ENABLE_HA_WS 1
    #else
        #define OF_ENABLE_HA_WS 0
    #endif
#endif

// Live log streaming over WebSocket: every emitted log line builds a JSON frame
// and pushes it to all clients. On constrained boards only forward Warning+ to
// cut the per-line heap churn (the full ring is still readable via the REST API).
// LogLevel::Warning == 3.
#ifndef OF_LOG_WS_MIN_LEVEL
    #if OF_CONSTRAINED
        #define OF_LOG_WS_MIN_LEVEL 3
    #else
        #define OF_LOG_WS_MIN_LEVEL 0
    #endif
#endif

// Largest accepted POST/WS body. A full 64 KB buffer is most of the ESP8266 heap,
// so cap constrained boards well below that.
#ifndef OF_MAX_POST_PAYLOAD
    #if OF_CONSTRAINED
        #define OF_MAX_POST_PAYLOAD (16 * 1024)
    #else
        #define OF_MAX_POST_PAYLOAD (64 * 1024)
    #endif
#endif

// Optional drivers. The library code (and its flash footprint) is only compiled
// when its #include is reached, so gating the include + registration here strips
// the driver entirely on boards that don't enable it. Heavy/rarely-paired drivers
// default off on constrained boards; override with -DOF_ENABLE_* in platformio.ini.
#ifndef OF_ENABLE_TFT          // ILI9341 / ST7735 / ST7789 SPI TFTs (large GFX libs)
    #define OF_ENABLE_TFT          (!OF_CONSTRAINED)
#endif
#ifndef OF_ENABLE_SENSOR_MPU6050
    #define OF_ENABLE_SENSOR_MPU6050 (!OF_CONSTRAINED)
#endif
#ifndef OF_ENABLE_SENSOR_SCD4X
    #define OF_ENABLE_SENSOR_SCD4X   (!OF_CONSTRAINED)
#endif
#ifndef OF_ENABLE_SENSOR_VL53L0X
    #define OF_ENABLE_SENSOR_VL53L0X (!OF_CONSTRAINED)
#endif
#ifndef OF_ENABLE_SENSOR_SGP30
    #define OF_ENABLE_SENSOR_SGP30   (!OF_CONSTRAINED)
#endif
#ifndef OF_ENABLE_SENSOR_MAX6675
    #define OF_ENABLE_SENSOR_MAX6675 (!OF_CONSTRAINED)
#endif
// Lightweight, library-free drivers (raw Wire / analogRead / pulseIn). Small flash
// cost, so on by default everywhere; override with -DOF_ENABLE_SENSOR_*=0.
#ifndef OF_ENABLE_SENSOR_AHT20        // AHT10/AHT20/AHT21 I²C temp+humidity
    #define OF_ENABLE_SENSOR_AHT20      1
#endif
#ifndef OF_ENABLE_SENSOR_DHT11        // DHT11 (reuses the already-linked DHT library)
    #define OF_ENABLE_SENSOR_DHT11      1
#endif
#ifndef OF_ENABLE_SENSOR_ANALOG       // generic ADC pin: value = raw*scale + offset
    #define OF_ENABLE_SENSOR_ANALOG     1
#endif
#ifndef OF_ENABLE_SENSOR_ULTRASONIC   // HC-SR04 trig/echo distance
    #define OF_ENABLE_SENSOR_ULTRASONIC 1
#endif
#ifndef OF_ENABLE_SENSOR_ADS1115      // ADS1115 4-channel I²C ADC
    #define OF_ENABLE_SENSOR_ADS1115    1
#endif
#ifndef OF_ENABLE_SENSOR_CCS811       // CCS811 eCO₂/TVOC air-quality I²C
    #define OF_ENABLE_SENSOR_CCS811     1
#endif
// UART sensors (MH-Z19 CO₂, PMS5003 particulate) need a spare HardwareSerial with
// remappable pins — ESP32 family only (the ESP8266 lacks free remappable UARTs).
#ifndef OF_ENABLE_SENSOR_UART
    #if defined(ESP32)
        #define OF_ENABLE_SENSOR_UART   1
    #else
        #define OF_ENABLE_SENSOR_UART   0
    #endif
#endif
// U8g2 OLED driver — used for the 0.42" 72x40 SSD1306 panel, which the Adafruit
// library can't init reliably (its sub-window/COM-pin remap doesn't drive these
// "ER" panels). Enabled on the ESP32 family incl. the flash-tight C3; off on the
// ESP8266. ~25-35 KB flash for one panel + a couple of fonts.
#ifndef OF_ENABLE_U8G2
    #if defined(ESP32)
        #define OF_ENABLE_U8G2 1
    #else
        #define OF_ENABLE_U8G2 0
    #endif
#endif

// microSD card support. Only the ESP32-S3-BOX ships a card slot we drive, but the
// driver compiles in across the S3 family so the self-test can report SD state.
// Mounting is attempted at boot for the box only (see main.cpp). The SD bus is the
// board's default hardware-SPI pins (SCK12/MISO13/MOSI11/CS10 on the S3-BOX
// variant), which is separate from the panel's own SPI bus (SCK7/MOSI6) — so SD
// and the display don't contend. Override the pins per board if your unit differs.
#ifndef OF_ENABLE_SD
    #if defined(ESP32S3_BOARD)
        #define OF_ENABLE_SD 1
    #else
        #define OF_ENABLE_SD 0
    #endif
#endif
#ifndef OF_BOX_SD_SCK
    #define OF_BOX_SD_SCK  12
    #define OF_BOX_SD_MISO 13
    #define OF_BOX_SD_MOSI 11
    #define OF_BOX_SD_CS   10
#endif

// Display image widgets (background + sprite). Images are pre-converted on the host
// to a tiny on-device format (OFIM: RGB565 for colour panels, 1-bit for mono), so
// the firmware never decodes JPEG/PNG — it just blits. Enabled on the ESP32 family
// (the ESP8266 is too flash/RAM-tight). The decoded frame is cached in PSRAM where
// available. Override per board with -DOF_ENABLE_IMAGES=0/1.
#ifndef OF_ENABLE_IMAGES
    #if defined(ESP32)
        #define OF_ENABLE_IMAGES 1
    #else
        #define OF_ENABLE_IMAGES 0
    #endif
#endif
// Largest accepted image upload. A 320×240 RGB565 frame is ~150 KB; cap with margin.
#ifndef OF_IMAGE_MAX_BYTES
    #define OF_IMAGE_MAX_BYTES (200 * 1024)
#endif

// Push notifications (PushNotifier): forwards NotificationManager posts to an
// external service (ntfy / Telegram / Pushover / generic webhook). Small code +
// a 4-entry outbound queue drained one POST per loop pass, so it's affordable on
// every board. TLS-only services (Telegram, Pushover, https ntfy/webhooks)
// additionally need OF_ENABLE_TLS — on the ESP8266 (TLS off, see below) only
// plain-http ntfy/webhook endpoints work.
#ifndef OF_ENABLE_PUSH
    #define OF_ENABLE_PUSH 1
#endif

// Weather integration (WeatherManager): polls Open-Meteo (keyless) and publishes
// live weather.* variables for the screen designer / actions. One bounded JSON
// fetch per interval + ~10 extra live variables — compiled out on constrained
// boards (heap/flash), on everywhere else. Override with -DOF_ENABLE_WEATHER.
#ifndef OF_ENABLE_WEATHER
    #define OF_ENABLE_WEATHER (!OF_CONSTRAINED)
#endif

// TLS / BearSSL. On the ESP8266 a single TLS handshake needs ~16-22 KB of heap —
// most of the ~25 KB free at boot — so MQTT-over-TLS or an HTTPS update check can
// OOM-crash the device, and BearSSL adds ~56 KB of flash. Gate it off there: the
// ESP8266 falls back to plain MQTT (the norm for a LAN broker) and skips the HTTPS
// GitHub release check (local upload + CMS push-OTA use plain HTTP and still work).
// ESP32-family TLS lives in the SDK, so it stays on there. Override with -DOF_ENABLE_TLS.
#ifndef OF_ENABLE_TLS
    #if defined(ESP8266_BOARD)
        #define OF_ENABLE_TLS 0
    #else
        #define OF_ENABLE_TLS 1
    #endif
#endif

// WebSocket
#define OF_WS_PATH "/ws"
