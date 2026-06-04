#include "OtaManager.h"

#include <LittleFS.h>

namespace {

struct FilesystemUploadState {
    bool started = false;
    bool finished = false;
    bool success = false;
    String error;
};

FilesystemUploadState* uploadState(AsyncWebServerRequest* request) {
    return static_cast<FilesystemUploadState*>(request->_tempObject);
}

FilesystemUploadState* ensureUploadState(AsyncWebServerRequest* request) {
    auto* state = uploadState(request);
    if (!state) {
        state = new FilesystemUploadState();
        request->_tempObject = state;
    }
    return state;
}

String updateErrorString() {
    return "Update error " + String(Update.getError());
}

}  // namespace

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
    registerFilesystemUploadRoute(server);

    ElegantOTA.onStart([this]() { onOtaStart(); });
    ElegantOTA.onProgress([this](size_t current, size_t total) {
        onOtaProgress(current, total);
    });
    ElegantOTA.onEnd([this](bool success) { onOtaEnd(success); });

    LOG_I(TAG, "OTA ready — upload at /update");
}

void OtaManager::loop() {
    ElegantOTA.loop();

    if (_restartPending && millis() >= _restartAtMs) {
        LOG_I(TAG, "Restarting after filesystem upload");
        ESP.restart();
    }
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

void OtaManager::registerFilesystemUploadRoute(AsyncWebServer& server) {
    server.on("/api/ota/filesystem", HTTP_POST,
        [this](AsyncWebServerRequest* request) {
            JsonDocument doc;
            auto* state = uploadState(request);

            if (state && state->success) {
                doc["status"] = "ok";
                doc["message"] = "Filesystem uploaded. Rebooting.";
                _restartPending = true;
                _restartAtMs = millis() + 1000;
            } else {
                doc["status"] = "error";
                doc["message"] = state ? state->error : "No filesystem image received";
                LittleFS.begin(false);
            }

            String body;
            serializeJson(doc, body);
            auto* response = request->beginResponse(state && state->success ? 200 : 400, "application/json", body);
            response->addHeader("Connection", "close");
            request->send(response);

            delete state;
            request->_tempObject = nullptr;
        },
        [this](AsyncWebServerRequest* request, const String&, size_t index, uint8_t* data, size_t len, bool final) {
            const size_t total = request->contentLength();
            handleFilesystemUploadChunk(request, index, data, len, total, final);
        },
        [this](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
            handleFilesystemUploadChunk(request, index, data, len, total, index + len == total);
        });

    LOG_I(TAG, "Filesystem upload ready at POST /api/ota/filesystem");
}

void OtaManager::handleFilesystemUploadChunk(
    AsyncWebServerRequest* request,
    size_t index,
    uint8_t* data,
    size_t len,
    size_t total,
    bool final) {
    auto* state = ensureUploadState(request);
    if (!state || state->finished || !state->error.isEmpty()) return;

    if (index == 0) {
        LOG_I(TAG, "Filesystem upload started");
        EventBus::instance().publish(EventType::OtaStarted, "filesystem", "");

        LittleFS.end();
        state->started = Update.begin(total > 0 ? total : UPDATE_SIZE_UNKNOWN, U_SPIFFS);
        if (!state->started) {
            state->error = updateErrorString();
            LOG_E(TAG, "Filesystem update begin failed: " + state->error);
            EventBus::instance().publish(EventType::OtaError, "filesystem", state->error);
            return;
        }
    }

    if (len > 0 && Update.write(data, len) != len) {
        state->error = updateErrorString();
        LOG_E(TAG, "Filesystem update write failed: " + state->error);
        EventBus::instance().publish(EventType::OtaError, "filesystem", state->error);
        Update.abort();
        return;
    }

    onOtaProgress(index + len, total);

    if (final) {
        state->finished = true;
        state->success = Update.end(true);
        if (state->success) {
            LOG_I(TAG, "Filesystem upload complete");
            EventBus::instance().publish(EventType::OtaFinished, "filesystem", "");
        } else {
            state->error = updateErrorString();
            LOG_E(TAG, "Filesystem update end failed: " + state->error);
            EventBus::instance().publish(EventType::OtaError, "filesystem", state->error);
        }
    }
}

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
