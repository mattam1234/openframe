#include "ApiServer.h"

#include <ArduinoJson.h>
#include <LittleFS.h>
#include <WiFi.h>
#include "../OpenFrameConfig.h"
#include "../core/ConfigManager.h"
#include "../hardware/DisplayManager.h"
#include "../managers/OtaManager.h"
#include "../managers/VariableManager.h"
#include "../managers/WiFiManager.h"

namespace {

String formatUptime(uint32_t uptimeMs) {
    const uint32_t totalSeconds = uptimeMs / 1000;
    const uint32_t days = totalSeconds / 86400;
    const uint32_t hours = (totalSeconds % 86400) / 3600;
    const uint32_t minutes = (totalSeconds % 3600) / 60;
    const uint32_t seconds = totalSeconds % 60;

    char buffer[32];
    if (days > 0) {
        snprintf(buffer, sizeof(buffer), "%lud %02lu:%02lu:%02lu",
            static_cast<unsigned long>(days),
            static_cast<unsigned long>(hours),
            static_cast<unsigned long>(minutes),
            static_cast<unsigned long>(seconds));
    } else {
        snprintf(buffer, sizeof(buffer), "%02lu:%02lu:%02lu",
            static_cast<unsigned long>(hours),
            static_cast<unsigned long>(minutes),
            static_cast<unsigned long>(seconds));
    }
    return String(buffer);
}

const char* logLevelToString(LogLevel level) {
    switch (level) {
        case LogLevel::Trace:   return "trace";
        case LogLevel::Debug:   return "debug";
        case LogLevel::Info:    return "info";
        case LogLevel::Warning: return "warning";
        case LogLevel::Error:   return "error";
        case LogLevel::Fatal:   return "fatal";
        default:                return "unknown";
    }
}

const char* varTypeToString(VarType type) {
    switch (type) {
        case VarType::Integer: return "integer";
        case VarType::Float:   return "float";
        case VarType::Boolean: return "boolean";
        case VarType::String:  return "string";
        default:               return "unknown";
    }
}

const char* eventTypeToString(EventType type) {
    switch (type) {
        case EventType::InputDigitalChanged: return "input_digital_changed";
        case EventType::InputAnalogChanged:  return "input_analog_changed";
        case EventType::OutputStateChanged:  return "output_state_changed";
        case EventType::SensorValueUpdated:  return "sensor_value_updated";
        case EventType::SensorError:         return "sensor_error";
        case EventType::DisplayPageChanged:  return "display_page_changed";
        case EventType::DisplayTouchEvent:   return "display_touch_event";
        case EventType::VariableChanged:     return "variable_changed";
        case EventType::WifiConnected:       return "wifi_connected";
        case EventType::WifiDisconnected:    return "wifi_disconnected";
        case EventType::MqttConnected:       return "mqtt_connected";
        case EventType::MqttDisconnected:    return "mqtt_disconnected";
        case EventType::MqttMessageReceived: return "mqtt_message_received";
        case EventType::HaConnected:         return "ha_connected";
        case EventType::HaDisconnected:      return "ha_disconnected";
        case EventType::ActionTriggered:     return "action_triggered";
        case EventType::MacroTriggered:      return "macro_triggered";
        case EventType::OtaStarted:          return "ota_started";
        case EventType::OtaProgress:         return "ota_progress";
        case EventType::OtaFinished:         return "ota_finished";
        case EventType::OtaError:            return "ota_error";
        case EventType::SystemHealthUpdate:  return "system_health_update";
        case EventType::NotificationPosted:  return "notification_posted";
        default:                             return "event";
    }
}

void serializeVariable(JsonObject obj, const Variable& var) {
    obj["id"] = var.id;
    obj["type"] = varTypeToString(var.type);
    obj["label"] = var.label;
    obj["persistent"] = var.persistent;

    switch (var.type) {
        case VarType::Integer: obj["value"] = var.valueInt; break;
        case VarType::Float:   obj["value"] = var.valueFloat; break;
        case VarType::Boolean: obj["value"] = var.valueBool; break;
        case VarType::String:  obj["value"] = var.valueString; break;
    }
}

void serializeLogEntry(JsonObject obj, const LogEntry& entry) {
    obj["timestamp"] = entry.timestamp;
    obj["level"] = logLevelToString(entry.level);
    obj["tag"] = entry.tag;
    obj["message"] = entry.message;
}

String toJsonString(const JsonDocument& doc) {
    String body;
    serializeJson(doc, body);
    return body;
}

}  // namespace

ApiServer& ApiServer::instance() {
    static ApiServer inst;
    return inst;
}

ApiServer::ApiServer()
    : _ws(OF_WS_PATH) {
}

void ApiServer::begin(AsyncWebServer& server) {
    if (_started) return;

    registerRoutes(server);
    registerWebSocket(server);
    server.serveStatic("/", LittleFS, "/www/").setDefaultFile("index.html");

    if (!_subscribed) {
        _subscribed = true;
        EventBus::instance().subscribeAll([](const Event& event) {
            ApiServer::instance().onEvent(event);
        });
        Logger::instance().addListener([](const LogEntry& entry) {
            ApiServer::instance().onLogEntry(entry);
        });
    }

    publishHealthUpdate();
    _started = true;
    LOG_I(TAG, "API server ready");
}

void ApiServer::loop() {
    if (!_started) return;
    _ws.cleanupClients();

    const uint32_t nowMs = millis();
    if (_lastHealthPublishMs == 0 || (nowMs - _lastHealthPublishMs) >= HEALTH_PUBLISH_INTERVAL_MS) {
        publishHealthUpdate();
    }
}

void ApiServer::registerRoutes(AsyncWebServer& server) {
    server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest* request) {
        ApiServer::instance().sendStatus(request);
    });

    server.on("/api/config", HTTP_GET, [](AsyncWebServerRequest* request) {
        ApiServer::instance().sendConfig(request);
    });
    server.on("/api/config", HTTP_POST,
        [](AsyncWebServerRequest*) {},
        nullptr,
        [](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
            String* body = index == 0 ? new String() : static_cast<String*>(request->_tempObject);
            if (index == 0) {
                body->reserve(total);
                request->_tempObject = body;
            }
            body->concat(reinterpret_cast<const char*>(data), len);
            if (index + len == total) {
                ApiServer::instance().handleConfigUpdate(request, *body);
                delete body;
                request->_tempObject = nullptr;
            }
        });

    server.on("/api/variables", HTTP_GET, [](AsyncWebServerRequest* request) {
        ApiServer::instance().sendVariables(request);
    });
    server.on("/api/variables", HTTP_POST,
        [](AsyncWebServerRequest*) {},
        nullptr,
        [](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
            String* body = index == 0 ? new String() : static_cast<String*>(request->_tempObject);
            if (index == 0) {
                body->reserve(total);
                request->_tempObject = body;
            }
            body->concat(reinterpret_cast<const char*>(data), len);
            if (index + len == total) {
                ApiServer::instance().handleVariablesUpdate(request, *body);
                delete body;
                request->_tempObject = nullptr;
            }
        });

    server.on("/api/logs", HTTP_GET, [](AsyncWebServerRequest* request) {
        ApiServer::instance().sendLogs(request);
    });

    server.on("/api/ota/check", HTTP_GET, [](AsyncWebServerRequest* request) {
        ApiServer::instance().sendOtaStatus(request);
    });
}

void ApiServer::registerWebSocket(AsyncWebServer& server) {
    _ws.onEvent([](AsyncWebSocket* ws, AsyncWebSocketClient* client, AwsEventType type, void* arg, uint8_t* data, size_t len) {
        (void)ws;

        if (type == WS_EVT_CONNECT) {
            ApiServer::instance().sendInitialState(client);
            return;
        }
        if (type != WS_EVT_DATA) return;

        auto* info = static_cast<AwsFrameInfo*>(arg);
        if (!info || info->opcode != WS_TEXT || !info->final || info->index != 0) return;

        String message;
        message.reserve(len);
        for (size_t i = 0; i < len; ++i) {
            message += static_cast<char>(data[i]);
        }
        ApiServer::instance().handleWebSocketMessage(client, message);
    });

    server.addHandler(&_ws);
}

void ApiServer::sendStatus(AsyncWebServerRequest* request) const {
    JsonDocument doc;
    deserializeJson(doc, buildStatusJson());
    sendJson(request, doc);
}

void ApiServer::sendConfig(AsyncWebServerRequest* request) const {
    JsonDocument doc;
    ConfigManager::instance().toJson(doc);
    sendJson(request, doc);
}

void ApiServer::sendVariables(AsyncWebServerRequest* request) const {
    JsonDocument doc;
    deserializeJson(doc, buildVariablesJson());
    sendJson(request, doc);
}

void ApiServer::sendLogs(AsyncWebServerRequest* request) const {
    JsonDocument doc;
    deserializeJson(doc, buildLogsJson());
    sendJson(request, doc);
}

void ApiServer::sendOtaStatus(AsyncWebServerRequest* request) const {
    JsonDocument doc;
    String latestVersion;
    String downloadUrl;
    const bool updateAvailable = OtaManager::instance().checkGitHubRelease(latestVersion, downloadUrl);

    doc["currentVersion"] = OF_VERSION_STRING;
    doc["updateAvailable"] = updateAvailable;
    doc["latestVersion"] = latestVersion;
    doc["downloadUrl"] = downloadUrl;
    sendJson(request, doc);
}

void ApiServer::handleConfigUpdate(AsyncWebServerRequest* request, const String& body) {
    JsonDocument doc;
    const DeserializationError error = deserializeJson(doc, body);
    if (error) {
        sendError(request, 400, "Invalid config JSON");
        return;
    }

    ConfigManager::instance().fromJson(doc);
    if (!ConfigManager::instance().save()) {
        sendError(request, 500, "Failed to save config");
        return;
    }

    JsonDocument response;
    ConfigManager::instance().toJson(response);
    sendJson(request, response);
    broadcastFrame("config_change", response);
    publishHealthUpdate();
}

void ApiServer::handleVariablesUpdate(AsyncWebServerRequest* request, const String& body) {
    JsonDocument doc;
    const DeserializationError parseError = deserializeJson(doc, body);
    if (parseError) {
        sendError(request, 400, "Invalid variable JSON");
        return;
    }

    String error;
    bool updated = false;

    if (doc["variables"].is<JsonArrayConst>()) {
        for (JsonVariantConst item : doc["variables"].as<JsonArrayConst>()) {
            if (!applyVariableUpdate(item, error)) {
                sendError(request, 400, error);
                return;
            }
            updated = true;
        }
    } else {
        if (!applyVariableUpdate(doc.as<JsonVariantConst>(), error)) {
            sendError(request, 400, error);
            return;
        }
        updated = true;
    }

    if (updated) {
        VariableManager::instance().save();
    }

    JsonDocument response;
    deserializeJson(response, buildVariablesJson());
    sendJson(request, response);
}

void ApiServer::handleWebSocketMessage(AsyncWebSocketClient* client, const String& message) {
    JsonDocument doc;
    const DeserializationError error = deserializeJson(doc, message);
    if (error) {
        JsonDocument response;
        response["ok"] = false;
        response["error"] = "Invalid WebSocket JSON";
        sendFrame(client, "command_result", response);
        return;
    }

    const String type = doc["type"] | String("");
    const JsonVariantConst payload = doc["payload"];

    JsonDocument response;
    response["type"] = type;
    response["ok"] = true;

    if (type == "page_navigation") {
        const String displayId = payload["displayId"] | String("");
        const String pageId = payload["pageId"] | String("");
        const bool ok = DisplayManager::instance().setActivePage(displayId, pageId);
        response["ok"] = ok;
        if (!ok) response["error"] = "Unknown display or page";
    } else if (type == "config_save" || type == "config_change") {
        if (!payload.is<JsonObjectConst>()) {
            response["ok"] = false;
            response["error"] = "Config payload must be an object";
            sendFrame(client, "command_result", response);
            return;
        }
        JsonDocument configDoc;
        configDoc.set(payload);
        ConfigManager::instance().fromJson(configDoc);
        if (!ConfigManager::instance().save()) {
            response["ok"] = false;
            response["error"] = "Failed to save config";
        } else {
            JsonDocument configResponse;
            ConfigManager::instance().toJson(configResponse);
            broadcastFrame("config_change", configResponse);
            publishHealthUpdate();
        }
    } else if (type == "action_trigger") {
        const String sourceId = payload["id"] | String("websocket");
        String payloadString;
        serializeJson(payload, payloadString);
        EventBus::instance().publish(EventType::ActionTriggered, sourceId, payloadString);
    } else if (type == "ping") {
        JsonDocument healthDoc;
        deserializeJson(healthDoc, buildStatusJson());
        sendFrame(client, "health", healthDoc);
        return;
    } else {
        response["ok"] = false;
        response["error"] = "Unknown command";
    }

    sendFrame(client, "command_result", response);
}

void ApiServer::sendInitialState(AsyncWebSocketClient* client) const {
    if (!client) return;

    JsonDocument statusDoc;
    deserializeJson(statusDoc, buildStatusJson());
    sendFrame(client, "health", statusDoc);

    const String variablesJson = buildVariablesJson();
    JsonDocument variablesDoc;
    deserializeJson(variablesDoc, variablesJson);
    String variablePayload;
    serializeJson(variablesDoc["variables"], variablePayload);
    sendRawFrame(client, "variable_snapshot", variablePayload);

    const String logsJson = buildLogsJson();
    JsonDocument logsDoc;
    deserializeJson(logsDoc, logsJson);
    String logPayload;
    serializeJson(logsDoc["entries"], logPayload);
    sendRawFrame(client, "log_snapshot", logPayload);
}

void ApiServer::publishHealthUpdate() {
    _lastHealthPublishMs = millis();
    const String payload = buildStatusJson();
    EventBus::instance().publish(EventType::SystemHealthUpdate, "system", payload);
}

void ApiServer::onEvent(const Event& event) {
    if (!_started) return;

    if (event.type == EventType::SystemHealthUpdate) {
        broadcastRawFrame("health", event.payload);
        return;
    }

    if (event.type == EventType::SensorValueUpdated && event.payload.length()) {
        JsonDocument rawDoc;
        if (!deserializeJson(rawDoc, event.payload)) {
            JsonDocument frameDoc;
            auto sensorObj = frameDoc[event.sourceId].to<JsonObject>();
            sensorObj["type"] = rawDoc["type"] | String("");
            sensorObj["timestamp_ms"] = rawDoc["timestamp_ms"] | 0;
            if (rawDoc["values"].is<JsonObjectConst>()) {
                sensorObj["values"].set(rawDoc["values"]);
            }
            broadcastFrame("sensor_update", frameDoc);
        }
    }

    if (event.type == EventType::VariableChanged) {
        const Variable* var = VariableManager::instance().get(event.sourceId);
        if (var) {
            JsonDocument doc;
            auto obj = doc[var->id].to<JsonObject>();
            serializeVariable(obj, *var);
            broadcastFrame("variable_change", doc);
        }
        return;
    }

    JsonDocument doc;
    doc["event"] = eventTypeToString(event.type);
    doc["sourceId"] = event.sourceId;
    doc["payload"] = event.payload;
    broadcastFrame("event", doc);
}

void ApiServer::onLogEntry(const LogEntry& entry) {
    if (!_started) return;

    JsonDocument doc;
    auto obj = doc.to<JsonObject>();
    serializeLogEntry(obj, entry);
    broadcastFrame("log", doc);
}

void ApiServer::sendJson(AsyncWebServerRequest* request, const JsonDocument& doc, int status) const {
    if (!request) return;
    request->send(status, "application/json", toJsonString(doc));
}

void ApiServer::sendError(AsyncWebServerRequest* request, int status, const String& message) const {
    if (!request) return;
    JsonDocument doc;
    doc["error"] = message;
    sendJson(request, doc, status);
}

void ApiServer::sendFrame(AsyncWebSocketClient* client, const char* type, const JsonDocument& payload) const {
    if (!client) return;
    const String payloadJson = toJsonString(payload);
    client->text("{\"type\":\"" + String(type) + "\",\"payload\":" + payloadJson + "}");
}

void ApiServer::sendRawFrame(AsyncWebSocketClient* client, const char* type, const String& rawPayloadJson) const {
    if (!client) return;
    const String payloadJson = rawPayloadJson.length() ? rawPayloadJson : String("null");
    client->text("{\"type\":\"" + String(type) + "\",\"payload\":" + payloadJson + "}");
}

void ApiServer::broadcastFrame(const char* type, const JsonDocument& payload) const {
    const String payloadJson = toJsonString(payload);
    _ws.textAll("{\"type\":\"" + String(type) + "\",\"payload\":" + payloadJson + "}");
}

void ApiServer::broadcastRawFrame(const char* type, const String& rawPayloadJson) const {
    const String payloadJson = rawPayloadJson.length() ? rawPayloadJson : String("null");
    _ws.textAll("{\"type\":\"" + String(type) + "\",\"payload\":" + payloadJson + "}");
}

String ApiServer::buildStatusJson() const {
    JsonDocument doc;
    const auto& config = ConfigManager::instance().config();
    const uint32_t uptimeMs = millis();
    const bool wifiConnected = WiFiManager::instance().isConnected();

    doc["name"] = config.device.name;
    doc["board"] = config.device.boardType;
    doc["version"] = OF_VERSION_STRING;
    doc["uptime_ms"] = uptimeMs;
    doc["uptime"] = formatUptime(uptimeMs);
    doc["freeHeap"] = ESP.getFreeHeap();
    doc["minFreeHeap"] = ESP.getMinFreeHeap();
    doc["wifiConnected"] = wifiConnected;
    doc["apMode"] = WiFiManager::instance().isApMode();
    doc["ip"] = WiFiManager::instance().localIP();
    doc["rssi"] = wifiConnected ? WiFi.RSSI() : 0;
    doc["mqttEnabled"] = config.mqtt.enabled;
    doc["haEnabled"] = config.ha.enabled;
    doc["otaEnabled"] = config.ota.enabled;
    doc["logCount"] = Logger::instance().getEntryCount();
    doc["variableCount"] = VariableManager::instance().all().size();
    doc["moduleCount"] = 0;
    return toJsonString(doc);
}

String ApiServer::buildVariablesJson() const {
    JsonDocument doc;
    auto variables = doc["variables"].to<JsonObject>();
    for (const Variable* var : VariableManager::instance().all()) {
        if (!var) continue;
        auto obj = variables[var->id].to<JsonObject>();
        serializeVariable(obj, *var);
    }
    return toJsonString(doc);
}

String ApiServer::buildLogsJson() const {
    JsonDocument doc;
    auto entries = doc["entries"].to<JsonArray>();
    for (const auto& entry : Logger::instance().getEntries()) {
        auto obj = entries.add<JsonObject>();
        serializeLogEntry(obj, entry);
    }
    doc["count"] = Logger::instance().getEntryCount();
    return toJsonString(doc);
}

bool ApiServer::applyVariableUpdate(const JsonVariantConst& item, String& error) const {
    if (!item.is<JsonObjectConst>()) {
        error = "Variable update must be an object";
        return false;
    }

    const JsonObjectConst object = item.as<JsonObjectConst>();
    const String id = item["id"] | String("");
    if (!id.length()) {
        error = "Variable id is required";
        return false;
    }

    const Variable* var = VariableManager::instance().get(id);
    if (!var) {
        error = "Unknown variable: " + id;
        return false;
    }

    if (!object.containsKey("value")) {
        error = "Variable value is required";
        return false;
    }

    switch (var->type) {
        case VarType::Integer:
            VariableManager::instance().setInt(id, item["value"] | 0);
            return true;
        case VarType::Float:
            VariableManager::instance().setFloat(id, item["value"] | 0.0f);
            return true;
        case VarType::Boolean:
            VariableManager::instance().setBool(id, item["value"] | false);
            return true;
        case VarType::String:
            VariableManager::instance().setString(id, item["value"] | String(""));
            return true;
    }

    error = "Unsupported variable type";
    return false;
}
