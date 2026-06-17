#include "CommandManager.h"

#include <ArduinoJson.h>
#include "../core/Logger.h"
#include "../core/ConfigManager.h"
#include "MqttManager.h"
#include "TelemetryManager.h"
#include "ProfileManager.h"
#include "WiFiManager.h"
#include "HealthMonitor.h"
#include "../core/EventBus.h"
#include "../hardware/OutputManager.h"

#if defined(ESP32)
    #include <HTTPUpdate.h>
#elif defined(ESP8266)
    #include <ESP8266httpUpdate.h>
#endif

CommandManager& CommandManager::instance() {
    static CommandManager inst;
    return inst;
}

void CommandManager::begin() {
    if (!ConfigManager::instance().config().mqtt.enabled) {
        LOG_I(TAG, "MQTT disabled — fleet commands off");
        return;
    }

    // subscribeRaw registers the topic so it is (re)subscribed on every connect.
    MqttManager::instance().subscribeRaw(
        MqttManager::instance().deviceTopic("cmd"),
        [this](const String& /*topic*/, const String& payload) { handle(payload); });

    LOG_I(TAG, "Listening for fleet commands");
}

void CommandManager::loop() {
    // Deferred reboot so the ack has a chance to flush before we restart.
    if (_rebootPending && (int32_t)(millis() - _rebootAtMs) >= 0) {
        LOG_W(TAG, "Rebooting on remote command");
        ESP.restart();
    }

    // Deferred OTA — runs after the "started" ack has been flushed.
    if (_otaUrl.length()) {
        const String url = _otaUrl;
        const String cmdId = _otaCmdId;
        _otaUrl = "";
        _otaCmdId = "";
        performOta(url, cmdId);
    }
}

void CommandManager::handle(const String& payload) {
    JsonDocument doc;
    if (deserializeJson(doc, payload)) {
        LOG_W(TAG, "Ignoring malformed command");
        return;
    }

    const String id   = doc["id"]   | String("");
    const String type = doc["type"] | String("");
    LOG_I(TAG, "Command '" + type + "' (id=" + id + ")");

    if (type == "get_status") {
        // Force a fresh heartbeat so the CMS gets current metrics immediately.
        TelemetryManager::instance().publishHeartbeat();
        ack(id, type, true);
    } else if (type == "get_profiles") {
        // Report this node's profiles so the CMS can offer a remote profile picker.
        JsonDocument out;
        out["id"]   = id;
        out["type"] = type;
        out["ok"]   = true;
        out["active"] = ProfileManager::instance().activeId();
        JsonArray arr = out["profiles"].to<JsonArray>();
        for (const auto& p : ProfileManager::instance().profiles()) {
            JsonObject o = arr.add<JsonObject>();
            o["id"]   = p.id;
            o["name"] = p.name;
        }
        String payload;
        serializeJson(out, payload);
        MqttManager::instance().publishDevice("cmd/result", payload, false);
    } else if (type == "identify") {
        LOG_I(TAG, "*** IDENTIFY *** I am '" + ConfigManager::instance().config().device.name +
                       "' (" + MqttManager::instance().deviceTopic("") + ")");
        // Flash the screen + web UI via a notification (works even in safe mode).
        EventBus::instance().publish(EventType::NotificationPosted, "identify",
                                     "{\"message\":\"Identify\"}");
        // Ack before the (blocking) blink so the CMS sees a prompt response.
        ack(id, type, true);
        // Blink LEDs / beep the buzzer when hardware is running.
        if (!HealthMonitor::instance().inSafeMode()) {
            OutputManager::instance().identify();
        }
    } else if (type == "reboot") {
        ack(id, type, true);
        _rebootPending = true;
        _rebootAtMs    = millis() + 500;
    } else if (type == "activate_profile") {
        // Switch what this node displays — mirrors POST /api/profiles/activate.
        const String profileId = doc["payload"]["id"] | String("");
        if (!profileId.length()) {
            ack(id, type, false, "missing profile id");
            return;
        }
        String err;
        if (!ProfileManager::instance().activate(profileId, err)) {
            ack(id, type, false, err);
            return;
        }
        ack(id, type, true);
    } else if (type == "apply_config") {
        // Push a (partial) config — mirrors POST /api/config: merge, persist, and
        // reboot to apply, exactly like the web settings path. The payload must
        // fit the MQTT buffer (see MqttManager::begin).
        JsonVariant cfg = doc["payload"];
        if (!cfg.is<JsonObject>()) {
            ack(id, type, false, "missing config payload");
            return;
        }
        JsonDocument cfgDoc;
        cfgDoc.set(cfg);
        ConfigManager::instance().fromJson(cfgDoc);
        if (!ConfigManager::instance().save()) {
            ack(id, type, false, "save failed");
            return;
        }
        ack(id, type, true);
        LOG_W(TAG, "Config applied remotely — rebooting to apply");
        _rebootPending = true;
        _rebootAtMs    = millis() + 800;
    } else if (type == "ota_url") {
        // Fleet OTA: download + flash firmware from a (LAN, plain-HTTP) URL —
        // typically served by the CMS. Deferred to loop() so this ack flushes first.
        const String url = doc["payload"]["url"] | String("");
        if (!url.length()) {
            ack(id, type, false, "missing url");
            return;
        }
        if (!WiFiManager::instance().isConnected()) {
            ack(id, type, false, "wifi not connected");
            return;
        }
        ack(id, type, true);  // accepted/started; success is observed as a version change
        _otaUrl   = url;
        _otaCmdId = id;
    } else {
        ack(id, type, false, "unknown command");
    }
}

void CommandManager::performOta(const String& url, const String& cmdId) {
    LOG_W(TAG, "Starting OTA from " + url);
    WiFiClient client;
    bool ok = false;
    String err;

#if defined(ESP32)
    httpUpdate.rebootOnUpdate(true);
    const t_httpUpdate_return r = httpUpdate.update(client, url);
    ok  = (r == HTTP_UPDATE_OK);
    err = httpUpdate.getLastErrorString();
#elif defined(ESP8266)
    ESPhttpUpdate.rebootOnUpdate(true);
    const t_httpUpdate_return r = ESPhttpUpdate.update(client, url);
    ok  = (r == HTTP_UPDATE_OK);
    err = ESPhttpUpdate.getLastErrorString();
#endif

    // On success the device has already rebooted into the new image; this only
    // runs on failure / no-update, so report it back on /cmd/result.
    LOG_W(TAG, "OTA result: " + err);
    ack(cmdId, "ota_url", ok, ok ? "" : err);
}

void CommandManager::ack(const String& id, const String& type, bool ok, const String& error) {
    JsonDocument doc;
    doc["id"]   = id;
    doc["type"] = type;
    doc["ok"]   = ok;
    if (error.length()) doc["error"] = error;

    String out;
    serializeJson(doc, out);
    MqttManager::instance().publishDevice("cmd/result", out, false);
}
