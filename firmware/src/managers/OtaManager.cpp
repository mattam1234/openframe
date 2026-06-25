#include "OtaManager.h"
#include "../OpenFrameConfig.h"

#include <LittleFS.h>
#if defined(ESP8266) && OF_ENABLE_TLS
#include <WiFiClientSecure.h>
#endif

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
        // The library destructor only free()s _tempObject without running the
        // destructor, so clean up ourselves if the client aborts mid-upload.
        request->onDisconnect([request]() {
            if (request->_tempObject) {
                delete static_cast<FilesystemUploadState*>(request->_tempObject);
                request->_tempObject = nullptr;
            }
        });
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

#if defined(ESP8266) && !OF_ENABLE_TLS
    // The GitHub releases API is HTTPS-only and TLS is compiled out on this board
    // (OF_ENABLE_TLS=0), so the auto update-check is unavailable here. Firmware can
    // still be updated via local upload (/update) or a CMS push-OTA over HTTP.
    (void)latestVersion;
    (void)downloadUrl;
    LOG_W(TAG, "GitHub update check unavailable: TLS not built in on this board");
    return false;
#else
    HTTPClient http;
  #if defined(ESP8266)
    // ESP8266 needs an explicit TLS client for the HTTPS GitHub API. There is no
    // on-device certificate store, so validation is skipped (setInsecure).
    WiFiClientSecure secureClient;
    secureClient.setInsecure();
    http.begin(secureClient, url);
  #else
    http.begin(url);  // ESP32 HTTPClient sets up TLS transport internally
  #endif
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
#endif  // ESP8266 && !OF_ENABLE_TLS
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
#if defined(ESP8266)
                LittleFS.begin();
#else
                LittleFS.begin(false);
#endif
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
#if defined(ESP8266)
        // ESP8266 targets the filesystem region with U_FS and needs a known size.
        state->started = total > 0 && Update.begin(total, U_FS);
#else
        state->started = Update.begin(total > 0 ? total : UPDATE_SIZE_UNKNOWN, U_SPIFFS);
#endif
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
#if defined(ESP8266)
        Update.end();  // ESP8266 UpdaterClass has no abort(); end() releases it
#else
        Update.abort();
#endif
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
