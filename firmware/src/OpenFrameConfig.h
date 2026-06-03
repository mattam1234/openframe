#pragma once

// Board detection helpers
#if defined(ESP32S3_BOARD)
    #define OF_BOARD_TYPE "ESP32-S3"
    #define OF_USB_HID_SUPPORTED 1
#elif defined(ESP32_BOARD)
    #define OF_BOARD_TYPE "ESP32"
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

// Default AP settings
#define OF_AP_SSID_PREFIX "OpenFrame-"
#define OF_AP_PASSWORD ""
#define OF_AP_IP "192.168.4.1"

// Storage
#define OF_CONFIG_PATH "/config.json"
#define OF_VARIABLES_PATH "/variables.json"
#define OF_INPUTS_PATH "/inputs.json"
#define OF_PROFILES_PATH "/profiles"
#define OF_TEMPLATES_PATH "/templates"
#define OF_DISPLAY_PAGES_PATH "/pages"
#define OF_ACTIONS_PATH "/actions.json"
#define OF_LOGS_PATH "/logs.json"

// Ring buffer sizes
#define OF_LOG_BUFFER_SIZE 1000
#define OF_ACTION_HISTORY_SIZE 1000

// WebSocket
#define OF_WS_PATH "/ws"
