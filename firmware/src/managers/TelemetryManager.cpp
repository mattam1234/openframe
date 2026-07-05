#include "TelemetryManager.h"

#include <ArduinoJson.h>
#include "../core/Logger.h"
#include "../core/ConfigManager.h"
#include "../OpenFrameConfig.h"
#include "MqttManager.h"
#include "WiFiManager.h"
#include "ProfileManager.h"
#include "HealthMonitor.h"

TelemetryManager& TelemetryManager::instance() {
    static TelemetryManager inst;
    return inst;
}

void TelemetryManager::begin() {
    if (!ConfigManager::instance().config().mqtt.enabled) {
        LOG_I(TAG, "MQTT disabled — heartbeat off");
        return;
    }
    LOG_I(TAG, "Heartbeat ready (every " + String(HEARTBEAT_INTERVAL_MS / 1000) + "s)");
}

void TelemetryManager::loop() {
    if (!MqttManager::instance().isConnected()) {
        _wasConnected = false;
        return;
    }

    const uint32_t now = millis();

    // Publish immediately on a fresh connection so the CMS sees current state
    // without waiting a full interval, then settle into the periodic cadence.
    if (!_wasConnected) {
        _wasConnected = true;
        publishHeartbeat();
        _lastPublishMs = now;
        return;
    }

    if (now - _lastPublishMs >= HEARTBEAT_INTERVAL_MS) {
        publishHeartbeat();
        _lastPublishMs = now;
    }
}

void TelemetryManager::publishHeartbeat() {
    if (!MqttManager::instance().isConnected()) return;
    // Retained so a CMS subscribing after the fact still gets the last snapshot.
    MqttManager::instance().publishDevice("status", buildHeartbeatJson(), true);
}

String TelemetryManager::buildHeartbeatJson() const {
    JsonDocument doc;
    const auto& config = ConfigManager::instance().config();
    const bool wifiConnected = WiFiManager::instance().isConnected();

    doc["deviceId"]        = WiFiManager::instance().deviceId();
    doc["name"]            = config.device.name;
    doc["board"]           = config.device.boardType;
    doc["version"]         = OF_VERSION_STRING;
    doc["ip"]              = WiFiManager::instance().localIP();
    doc["rssi"]            = wifiConnected ? WiFi.RSSI() : 0;
    doc["freeHeap"]        = ESP.getFreeHeap();
    doc["uptimeMs"]        = millis();
    doc["cpuLoadPercent"]  = static_cast<int>(HealthMonitor::instance().getCpuLoadPercent());
    doc["activeProfileId"] = ProfileManager::instance().activeId();

    // Build-time feature flags (mirrors /api/features) so the CMS can show what
    // each node supports without an extra per-device round-trip.
    JsonObject features = doc["features"].to<JsonObject>();
    features["weather"] = (OF_ENABLE_WEATHER != 0);
    features["push"]    = (OF_ENABLE_PUSH != 0);
    features["ha_ws"]   = (OF_ENABLE_HA_WS != 0);
    features["tft"]     = (OF_ENABLE_TFT != 0);
    features["tls"]     = (OF_ENABLE_TLS != 0);

    String out;
    serializeJson(doc, out);
    return out;
}
