#pragma once

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <ElegantOTA.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "../core/Logger.h"
#include "../core/EventBus.h"
#include "../core/ConfigManager.h"
#include "../OpenFrameConfig.h"

// ── OtaManager ────────────────────────────────────────────────────────────────
//
// Handles:
//  • Local firmware upload via ElegantOTA (served on /update)
//  • GitHub release check — GET /api/ota/check returns latest tag + download URL
//  • OTA progress events broadcast to EventBus (OtaStarted, OtaProgress, OtaFinished, OtaError)
//  • Rollback is inherently supported by the ESP32 dual-partition OTA scheme

class OtaManager {
public:
    static OtaManager& instance();

    // Call once in setup(), passing the shared AsyncWebServer instance
    void begin(AsyncWebServer& server);

    // Call every loop()
    void loop();

    // Trigger a GitHub release check.
    // Returns true if a newer release is available; fills `latestVersion` and `downloadUrl`.
    bool checkGitHubRelease(String& latestVersion, String& downloadUrl);

private:
    OtaManager() = default;

    void onOtaStart();
    void onOtaProgress(size_t current, size_t total);
    void onOtaEnd(bool success);

    static constexpr const char* GITHUB_RELEASES_API =
        "https://api.github.com/repos/%s/releases/latest";
    static constexpr const char* TAG = "OTA";
};
