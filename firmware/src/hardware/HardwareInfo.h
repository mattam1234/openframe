#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include "../core/PlatformCompat.h"
#include "../OpenFrameConfig.h"

// ── Runtime hardware self-report ──────────────────────────────────────────────
//
// Fills a JsonObject with everything the firmware can learn about the SoC it is
// running on at runtime: chip model, revision, core count, clock, flash/PSRAM
// sizes, heap headroom and sketch usage. This is the runtime counterpart to the
// compile-time OF_BOARD_TYPE / BOARD_NAME build defines.
//
// Header-only (inline) so it can be pulled in wherever needed without a TU.

inline void fillHardwareInfo(JsonObject obj) {
    // Compile-time identity
    obj["board_type"] = OF_BOARD_TYPE;            // e.g. "ESP32-S3"
#ifdef BOARD_NAME
    obj["build_env"]  = BOARD_NAME;               // e.g. "esp32s3geek"
#endif
    obj["firmware"]   = OF_VERSION_STRING;

    // SoC identity
#if defined(OF_PLATFORM_ESP32)
    obj["chip_model"]    = ESP.getChipModel();
    obj["chip_revision"] = ESP.getChipRevision();
    obj["cpu_cores"]     = ESP.getChipCores();
#else
    obj["chip_model"]    = "ESP8266";
    obj["chip_revision"] = 0;
    obj["cpu_cores"]     = 1;
#endif
    obj["cpu_freq_mhz"]  = ESP.getCpuFreqMHz();
    obj["sdk_version"]   = ESP.getSdkVersion();
    obj["mac"]           = WiFi.macAddress();

    // Flash
    obj["flash_size"]      = ESP.getFlashChipSize();
    obj["flash_speed_hz"]  = ESP.getFlashChipSpeed();

    // PSRAM (0 when absent, e.g. ESP8266 or non-PSRAM ESP32)
    const uint32_t psram = of_psram_size();
    obj["psram_present"] = psram > 0;
    obj["psram_size"]    = psram;
    obj["psram_free"]    = of_free_psram();

    // Heap
    obj["heap_free"]     = ESP.getFreeHeap();
    obj["heap_min_free"] = of_min_free_heap();

    // Sketch / OTA partition
    obj["sketch_size"]   = ESP.getSketchSize();
    obj["sketch_free"]   = ESP.getFreeSketchSpace();

    obj["reset_reason"]  = of_reboot_reason();
}
