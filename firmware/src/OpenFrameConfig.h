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
#define OF_ACTIONS_PATH "/actions.json"
#define OF_MACROS_PATH "/macros.json"
#define OF_SCENES_PATH "/scenes.json"
#define OF_LOGS_PATH "/logs.json"
#define OF_NOTIFICATIONS_PATH "/notifications.json"
#define OF_CRASHLOG_PATH "/crashlog.json"
#define OF_MQTT_CA_PATH "/mqtt_ca.pem"
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
    #else
        #define OF_LOG_BUFFER_SIZE 1000
    #endif
#endif
#ifndef OF_ACTION_HISTORY_SIZE
    #if OF_CONSTRAINED
        #define OF_ACTION_HISTORY_SIZE 100
    #else
        #define OF_ACTION_HISTORY_SIZE 1000
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

// WebSocket
#define OF_WS_PATH "/ws"
