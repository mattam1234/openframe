#include "OtaManager.h"

OtaManager& OtaManager::instance() {
    static OtaManager inst;
    return inst;
}

// ── Public ────────────────────────────────────────────────────────────────────

void OtaManager::begin(AsyncWebServer& server) {
    if (!ConfigManager::instance().config().ota.enabled) {
        LOG_I(TAG, "OTA disabled in config");
        return;
    }

    ElegantOTA.begin(&server);

    ElegantOTA.onStart([this]() { onOtaStart(); });
    ElegantOTA.onProgress([this](size_t current, size_t total) {
        onOtaProgress(current, total);
    });
    ElegantOTA.onEnd([this](bool success) { onOtaEnd(success); });

    LOG_I(TAG, "OTA ready — upload at /update");
}

void OtaManager::loop() {
    ElegantOTA.loop();
}

bool OtaManager::checkGitHubRelease(String& latestVersion, String& downloadUrl) {
    const String& repo = ConfigManager::instance().config().ota.githubRepo;
    if (repo.isEmpty()) {
        LOG_W(TAG, "GitHub repo not configured");
        return false;
    }

    char url[128];
    snprintf(url, sizeof(url), GITHUB_RELEASES_API, repo.c_str());

    HTTPClient http;
    http.begin(url);
    http.addHeader("User-Agent", "OpenFrame/" OF_VERSION_STRING);
    http.addHeader("Accept", "application/vnd.github+json");

    int code = http.GET();
    if (code != 200) {
        LOG_W(TAG, "GitHub API returned HTTP " + String(code));
        http.end();
        return false;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, http.getString());
    http.end();

    if (err) {
        LOG_E(TAG, "JSON parse error: " + String(err.c_str()));
        return false;
    }

    latestVersion = doc["tag_name"] | String("");
    // Find firmware binary asset
    for (auto asset : doc["assets"].as<JsonArray>()) {
        String name = asset["name"] | String("");
        if (name.endsWith(".bin")) {
            downloadUrl = asset["browser_download_url"] | String("");
            break;
        }
    }

    if (latestVersion.isEmpty()) return false;

    // Strip leading 'v' for comparison
    String current = OF_VERSION_STRING;
    String latest  = latestVersion;
    if (latest.startsWith("v")) latest = latest.substring(1);

    bool newer = (latest != current);
    if (newer) {
        LOG_I(TAG, "New release available: " + latestVersion);
    } else {
        LOG_I(TAG, "Firmware is up to date (" + current + ")");
    }
    return newer;
}

// ── Private ───────────────────────────────────────────────────────────────────

void OtaManager::onOtaStart() {
    LOG_I(TAG, "OTA update started");
    _otaProgressPct = 0;
    EventBus::instance().publish(EventType::OtaStarted, "ota", "");
}

void OtaManager::onOtaProgress(size_t current, size_t total) {
    if (total == 0) return;
    uint8_t pct = (uint8_t)((current * 100UL) / total);

    // Only log every 10%
    if (pct >= _otaProgressPct + 10 || pct == 100) {
        LOG_I(TAG, "OTA progress: " + String(pct) + "%");
        _otaProgressPct = pct;
    }

    JsonDocument doc;
    doc["current"] = current;
    doc["total"]   = total;
    doc["percent"] = pct;
    String payload;
    serializeJson(doc, payload);
    EventBus::instance().publish(EventType::OtaProgress, "ota", payload);
}

void OtaManager::onOtaEnd(bool success) {
    if (success) {
        LOG_I(TAG, "OTA complete — rebooting");
        EventBus::instance().publish(EventType::OtaFinished, "ota", "");
    } else {
        LOG_E(TAG, "OTA failed");
        EventBus::instance().publish(EventType::OtaError, "ota", "OTA update failed");
    }
}
