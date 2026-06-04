#include "ApiServer.h"

#include <ArduinoJson.h>
#include <LittleFS.h>
#include "../core/PlatformCompat.h"
#include "../OpenFrameConfig.h"
#include "../core/ConfigManager.h"
#include "../hardware/InputManager.h"
#include "../hardware/OutputManager.h"
#include "../hardware/SensorManager.h"
#include "../hardware/DisplayManager.h"
#include "../hardware/ModuleManager.h"
#include "../managers/ActionEngine.h"
#include "../managers/MacroManager.h"
#include "../managers/OtaManager.h"
#include "../managers/VariableManager.h"
#include "../managers/WiFiManager.h"
#include "../managers/ProfileManager.h"
#include "../managers/HealthMonitor.h"
#include "../managers/NotificationManager.h"

namespace {

String formatUptime(uint32_t uptimeMs) {
    const uint32_t totalSeconds = uptimeMs / 1000;
    const uint32_t days = totalSeconds / 86400;
    const uint32_t hours = (totalSeconds % 86400) / 3600;
    const uint32_t minutes = (totalSeconds % 3600) / 60;
    const uint32_t seconds = totalSeconds % 60;

    char buffer[32];
    if (days > 0) {
        snprintf(buffer, sizeof(buffer), "%lu d %02lu:%02lu:%02lu",
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

const char* contentTypeForPath(const String& path) {
    if (path.endsWith(".html")) return "text/html";
    if (path.endsWith(".css")) return "text/css";
    if (path.endsWith(".js")) return "application/javascript";
    if (path.endsWith(".json")) return "application/json";
    if (path.endsWith(".svg")) return "image/svg+xml";
    if (path.endsWith(".png")) return "image/png";
    if (path.endsWith(".jpg") || path.endsWith(".jpeg")) return "image/jpeg";
    if (path.endsWith(".webp")) return "image/webp";
    if (path.endsWith(".ico")) return "image/x-icon";
    if (path.endsWith(".woff2")) return "font/woff2";
    if (path.endsWith(".woff")) return "font/woff";
    if (path.endsWith(".ttf")) return "font/ttf";
    return "application/octet-stream";
}

void sendStaticFile(AsyncWebServerRequest* request, const String& path, const char* cacheControl) {
    String filePath = path;
    bool gzip = false;

    if (!LittleFS.exists(filePath)) {
        const String gzipPath = filePath + ".gz";
        if (LittleFS.exists(gzipPath)) {
            filePath = gzipPath;
            gzip = true;
        }
    } else if (filePath.endsWith(".gz")) {
        gzip = true;
    }

    if (!LittleFS.exists(filePath)) {
        request->send(404, "text/plain", "Not found");
        return;
    }

    String typePath = filePath;
    if (typePath.endsWith(".gz")) {
        typePath.remove(typePath.length() - 3);
    }

    auto* response = request->beginResponse(LittleFS, filePath, contentTypeForPath(typePath));
    response->addHeader("Cache-Control", cacheControl);
    if (gzip) {
        response->addHeader("Content-Encoding", "gzip");
    }
    request->send(response);
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

    const bool hasIndexHtml = StorageManager::instance().exists("/www/index.html");
    const bool hasIndexHtmlGz = hasIndexHtml ? false : StorageManager::instance().exists("/www/index.html.gz");

    if (hasIndexHtml || hasIndexHtmlGz) {
        server.on("/assets/*", HTTP_GET, [](AsyncWebServerRequest* request) {
            const String path = "/www" + request->url();
            sendStaticFile(request, path, "public, max-age=31536000, immutable");
        });

        server.on("/favicon.svg", HTTP_GET, [](AsyncWebServerRequest* request) {
            sendStaticFile(request, "/www/favicon.svg", "public, max-age=86400");
        });

        server.on("/", HTTP_GET, [hasIndexHtml](AsyncWebServerRequest* request) {
            sendStaticFile(request, hasIndexHtml ? "/www/index.html" : "/www/index.html.gz", "no-store");
        });

        server.onNotFound([hasIndexHtml](AsyncWebServerRequest* request) {
            const String url = request->url();
            if (url.startsWith("/api/") || url.startsWith("/assets/") || url.indexOf('.') >= 0) {
                request->send(404, "text/plain", "Not found");
                return;
            }

            sendStaticFile(request, hasIndexHtml ? "/www/index.html" : "/www/index.html.gz", "no-store");
        });
    } else {
        server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
            request->send(200, "text/html",
                "<!doctype html><html><head><meta charset='utf-8'><title>OpenFrame</title></head>"
                "<body><h1>OpenFrame</h1><p>No web assets were found in /www.</p>"
                "<p>Upload your filesystem image to flash the UI.</p>"
                "<p>OTA page: <a href='/update'>/update</a></p></body></html>");
        });
        LOG_I(TAG, "No web assets found under /www; static file serving disabled");
    }

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
    if (_restartPending && nowMs >= _restartAtMs) {
        LOG_I(TAG, "Restarting to apply updated settings");
        ESP.restart();
    }

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
    server.on("/api/wifi/scan", HTTP_GET, [](AsyncWebServerRequest* request) {
        ApiServer::instance().sendWifiScan(request);
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

    server.on("/api/inputs", HTTP_GET, [](AsyncWebServerRequest* request) {
        ApiServer::instance().sendInputs(request);
    });
    server.on("/api/inputs", HTTP_POST,
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
                ApiServer::instance().handleInputsUpdate(request, *body);
                delete body;
                request->_tempObject = nullptr;
            }
        });

    server.on("/api/outputs", HTTP_GET, [](AsyncWebServerRequest* request) {
        ApiServer::instance().sendOutputs(request);
    });
    server.on("/api/outputs", HTTP_POST,
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
                ApiServer::instance().handleOutputsUpdate(request, *body);
                delete body;
                request->_tempObject = nullptr;
            }
        });

    server.on("/api/sensors", HTTP_GET, [](AsyncWebServerRequest* request) {
        ApiServer::instance().sendSensors(request);
    });
    server.on("/api/sensors", HTTP_POST,
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
                ApiServer::instance().handleSensorsUpdate(request, *body);
                delete body;
                request->_tempObject = nullptr;
            }
        });

    server.on("/api/displays", HTTP_GET, [](AsyncWebServerRequest* request) {
        ApiServer::instance().sendDisplays(request);
    });
    server.on("/api/displays", HTTP_POST,
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
                ApiServer::instance().handleDisplaysUpdate(request, *body);
                delete body;
                request->_tempObject = nullptr;
            }
        });
    server.on("/api/displays/pages", HTTP_GET, [](AsyncWebServerRequest* request) {
        ApiServer::instance().sendDisplayPages(request);
    });

    server.on("/api/modules", HTTP_GET, [](AsyncWebServerRequest* request) {
        ApiServer::instance().sendModules(request);
    });

    server.on("/api/actions", HTTP_GET, [](AsyncWebServerRequest* request) {
        ApiServer::instance().sendActions(request);
    });
    server.on("/api/actions", HTTP_POST,
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
                ApiServer::instance().handleActionsUpdate(request, *body);
                delete body;
                request->_tempObject = nullptr;
            }
        });

    server.on("/api/macros", HTTP_GET, [](AsyncWebServerRequest* request) {
        ApiServer::instance().sendMacros(request);
    });
    server.on("/api/macros", HTTP_POST,
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
                ApiServer::instance().handleMacrosUpdate(request, *body);
                delete body;
                request->_tempObject = nullptr;
            }
        });

    // ── Profiles ──────────────────────────────────────────────────────────────
    server.on("/api/profiles", HTTP_GET, [](AsyncWebServerRequest* request) {
        ApiServer::instance().sendProfiles(request);
    });
    server.on("/api/profiles", HTTP_POST,
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
                ApiServer::instance().handleProfileCreate(request, *body);
                delete body;
                request->_tempObject = nullptr;
            }
        });
    server.on("/api/profiles/activate", HTTP_POST,
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
                ApiServer::instance().handleProfileActivate(request, *body);
                delete body;
                request->_tempObject = nullptr;
            }
        });
    server.on("/api/profiles/*", HTTP_DELETE, [](AsyncWebServerRequest* request) {
        const String url = request->url();
        const String profileId = url.substring(url.lastIndexOf('/') + 1);
        ApiServer::instance().handleProfileDelete(request, profileId);
    });

    // ── Templates ─────────────────────────────────────────────────────────────
    server.on("/api/templates", HTTP_GET, [](AsyncWebServerRequest* request) {
        ApiServer::instance().sendTemplates(request);
    });
    server.on("/api/templates/export", HTTP_POST,
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
                ApiServer::instance().handleTemplateExport(request, *body);
                delete body;
                request->_tempObject = nullptr;
            }
        });
    server.on("/api/templates/import", HTTP_POST,
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
                ApiServer::instance().handleTemplateImport(request, *body);
                delete body;
                request->_tempObject = nullptr;
            }
        });
    server.on("/api/templates/*", HTTP_GET, [](AsyncWebServerRequest* request) {
        const String url = request->url();
        const String templateId = url.substring(url.lastIndexOf('/') + 1);
        ApiServer::instance().sendTemplateById(request, templateId);
    });
    server.on("/api/templates/*", HTTP_DELETE, [](AsyncWebServerRequest* request) {
        const String url = request->url();
        const String templateId = url.substring(url.lastIndexOf('/') + 1);
        const bool removed = StorageManager::instance().remove("/templates/" + templateId + ".json");
        JsonDocument doc;
        if (!removed) {
            doc["ok"]    = false;
            doc["error"] = "Template not found";
            String s;
            serializeJson(doc, s);
            request->send(404, "application/json", s);
            return;
        }
        doc["ok"] = true;
        doc["id"] = templateId;
        String s;
        serializeJson(doc, s);
        request->send(200, "application/json", s);
    });

    // ── Notifications ─────────────────────────────────────────────────────────
    server.on("/api/notifications", HTTP_GET, [](AsyncWebServerRequest* request) {
        ApiServer::instance().sendNotifications(request);
    });
    server.on("/api/notifications/read", HTTP_POST,
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
                JsonDocument doc;
                if (deserializeJson(doc, *body) == DeserializationError::Ok && doc["id"].is<const char*>()) {
                    NotificationManager::instance().markRead(doc["id"].as<String>());
                } else {
                    NotificationManager::instance().markAllRead();
                }
                delete body;
                request->_tempObject = nullptr;
                JsonDocument resp;
                resp["ok"] = true;
                String s;
                serializeJson(resp, s);
                request->send(200, "application/json", s);
            }
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

void ApiServer::sendWifiScan(AsyncWebServerRequest* request) const {
    JsonDocument doc;
    WiFiManager::instance().scanNearbyNetworks(doc);
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

    size_t postedWifiNetworks = 0;
    if (doc["wifi"]["networks"].is<JsonArrayConst>()) {
        postedWifiNetworks = doc["wifi"]["networks"].as<JsonArrayConst>().size();
    }

    LOG_I(TAG,
          "Config POST: bytes=" + String(body.length()) +
          ", device=" + String(doc["device"]["name"] | "") +
          ", wifiSsid=" + String(doc["wifi"]["ssid"] | "") +
          ", wifiNetworks=" + String(postedWifiNetworks) +
          ", apMode=" + String((doc["wifi"]["ap_mode"] | true) ? "true" : "false") +
          ", mqtt=" + String((doc["mqtt"]["enabled"] | false) ? "enabled" : "disabled") +
          ", ha=" + String((doc["ha"]["enabled"] | false) ? "enabled" : "disabled"));

    ConfigManager::instance().fromJson(doc);
    if (!ConfigManager::instance().save()) {
        sendError(request, 500, "Failed to save config");
        return;
    }

    JsonDocument response;
    ConfigManager::instance().toJson(response);
    response["restartRequired"] = true;
    response["message"] = "Settings saved. Restarting to apply changes.";
    sendJson(request, response);
    broadcastFrame("config_change", response);
    publishHealthUpdate();

    scheduleRestart();
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

void ApiServer::scheduleRestart(uint32_t delayMs) {
    _restartPending = true;
    _restartAtMs = millis() + delayMs;
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

void ApiServer::broadcastFrame(const char* type, const JsonDocument& payload) {
    const String payloadJson = toJsonString(payload);
    _ws.textAll("{\"type\":\"" + String(type) + "\",\"payload\":" + payloadJson + "}");
}

void ApiServer::broadcastRawFrame(const char* type, const String& rawPayloadJson) {
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
    doc["minFreeHeap"] = of_min_free_heap();
    doc["freePsram"] = of_free_psram();
    doc["psramSize"] = of_psram_size();
    doc["cpuLoadPercent"] = static_cast<int>(HealthMonitor::instance().getCpuLoadPercent());
    doc["rebootReason"] = HealthMonitor::instance().getRebootReason();
    doc["wifiConnected"] = wifiConnected;
    doc["apMode"] = WiFiManager::instance().isApMode();
    doc["ip"] = WiFiManager::instance().localIP();
    doc["rssi"] = wifiConnected ? WiFi.RSSI() : 0;
    doc["mqttEnabled"] = config.mqtt.enabled;
    doc["haEnabled"] = config.ha.enabled;
    doc["otaEnabled"] = config.ota.enabled;
    doc["logCount"] = Logger::instance().getEntryCount();
    doc["variableCount"] = VariableManager::instance().all().size();
    doc["moduleCount"] = ModuleManager::instance().modules().size();
    doc["i2cErrorCount"] = ModuleManager::instance().i2cErrorCount();

    // Sensor failure flags
    auto sensorArr = doc["sensors"].to<JsonArray>();
    uint32_t sensorErrorTotal = 0;
    for (const auto& inst : SensorManager::instance().sensors()) {
        if (!inst.healthy || inst.errorCount > 0) {
            auto obj = sensorArr.add<JsonObject>();
            obj["id"]         = inst.config.id;
            obj["healthy"]    = inst.healthy;
            obj["errorCount"] = inst.errorCount;
            if (inst.lastError.length()) obj["lastError"] = inst.lastError;
            sensorErrorTotal += inst.errorCount;
        }
    }
    doc["sensorErrorCount"] = sensorErrorTotal;

    doc["activeProfileId"] = ProfileManager::instance().activeId();

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

void ApiServer::sendInputs(AsyncWebServerRequest* request) const {
    JsonDocument doc;
    if (!StorageManager::instance().readJson(OF_INPUTS_PATH, doc)) {
        doc["digital"].to<JsonArray>();
        doc["analog"].to<JsonArray>();
    }
    doc["digitalCount"] = doc["digital"].is<JsonArrayConst>() ? doc["digital"].as<JsonArrayConst>().size() : 0;
    doc["analogCount"] = doc["analog"].is<JsonArrayConst>() ? doc["analog"].as<JsonArrayConst>().size() : 0;
    sendJson(request, doc);
}

void ApiServer::sendOutputs(AsyncWebServerRequest* request) const {
    JsonDocument doc;
    if (!StorageManager::instance().readJson(OF_OUTPUTS_PATH, doc)) {
        doc["outputs"].to<JsonArray>();
    }
    doc["count"] = doc["outputs"].is<JsonArrayConst>() ? doc["outputs"].as<JsonArrayConst>().size() : 0;
    sendJson(request, doc);
}

void ApiServer::sendSensors(AsyncWebServerRequest* request) const {
    JsonDocument doc;
    if (!StorageManager::instance().readJson(OF_SENSORS_PATH, doc)) {
        auto arr = doc["sensors"].to<JsonArray>();
        for (const auto& inst : SensorManager::instance().sensors()) {
            auto obj = arr.add<JsonObject>();
            obj["id"] = inst.config.id;
            obj["type"] = inst.config.type;
            obj["enabled"] = inst.config.enabled;
            obj["poll_interval_ms"] = inst.config.pollIntervalMs;
            obj["variable_prefix"] = inst.config.variablePrefix;
            obj["address"] = inst.config.address;
            obj["pin"] = inst.config.pin;
        }
    }
    doc["count"] = doc["sensors"].is<JsonArrayConst>() ? doc["sensors"].as<JsonArrayConst>().size() : 0;
    sendJson(request, doc);
}

void ApiServer::sendDisplays(AsyncWebServerRequest* request) const {
    JsonDocument doc;
    if (!StorageManager::instance().readJson(OF_DISPLAYS_PATH, doc)) {
        auto arr = doc["displays"].to<JsonArray>();
        for (const auto& cfg : DisplayManager::instance().displayConfigs()) {
            auto obj = arr.add<JsonObject>();
            obj["id"] = cfg.id;
            obj["type"] = cfg.type;
            obj["enabled"] = cfg.enabled;
            obj["width"] = cfg.width;
            obj["height"] = cfg.height;
            obj["address"] = cfg.address;
        }
    }
    doc["count"] = doc["displays"].is<JsonArrayConst>() ? doc["displays"].as<JsonArrayConst>().size() : 0;
    sendJson(request, doc);
}

void ApiServer::sendDisplayPages(AsyncWebServerRequest* request) const {
    JsonDocument doc;
    auto arr = doc["pages"].to<JsonArray>();

    const auto files = StorageManager::instance().listDir(OF_DISPLAY_PAGES_PATH);
    for (const auto& entry : files) {
        String path = entry;
        if (!path.startsWith("/")) {
            path = String(OF_DISPLAY_PAGES_PATH) + "/" + path;
        }

        JsonDocument pageDoc;
        if (!StorageManager::instance().readJson(path, pageDoc)) continue;

        auto page = arr.add<JsonObject>();
        String id = pageDoc["id"] | String("");
        if (!id.length()) {
            int slash = path.lastIndexOf('/');
            String name = slash >= 0 ? path.substring(slash + 1) : path;
            if (name.endsWith(".json")) name.remove(name.length() - 5);
            id = name;
        }
        page["id"] = id;
        page["title"] = pageDoc["title"] | id;
        page["displayId"] = pageDoc["display_id"] | String("");

        auto widgets = page["widgets"].to<JsonArray>();
        if (pageDoc["widgets"].is<JsonArrayConst>()) {
            for (JsonObjectConst item : pageDoc["widgets"].as<JsonArrayConst>()) {
                auto out = widgets.add<JsonObject>();
                out["id"] = item["id"] | String("");
                out["type"] = item["type"] | String("text");
                out["x"] = item["x"] | 0;
                out["y"] = item["y"] | 0;
                out["textSize"] = item["text_size"] | 1;
                out["text"] = item["text"] | String("");
                out["variableId"] = item["variable"] | String("");
                out["prefix"] = item["prefix"] | String("");
                out["suffix"] = item["suffix"] | String("");
                out["decimals"] = item["decimals"] | 1;
                out["maxChars"] = item["max_chars"] | 0;
            }
        }
    }

    doc["count"] = arr.size();
    sendJson(request, doc);
}

void ApiServer::handleInputsUpdate(AsyncWebServerRequest* request, const String& body) {
    JsonDocument source;
    if (deserializeJson(source, body)) {
        sendError(request, 400, "Invalid JSON");
        return;
    }

    JsonDocument toSave;
    auto digital = toSave["digital"].to<JsonArray>();
    auto analog = toSave["analog"].to<JsonArray>();

    if (source["digital"].is<JsonArrayConst>()) {
        for (JsonObjectConst item : source["digital"].as<JsonArrayConst>()) {
            auto out = digital.add<JsonObject>();
            out["id"] = item["id"] | String("");
            out["pin"] = item["pin"] | 0;
            out["pullup"] = item["pullup"] | true;
            out["inverted"] = item["inverted"] | false;
            if (item["pulldown"].is<bool>()) out["pulldown"] = item["pulldown"].as<bool>();
            if (item["debounce_ms"].is<int>()) out["debounce_ms"] = item["debounce_ms"].as<int>();
            if (item["hold_ms"].is<int>()) out["hold_ms"] = item["hold_ms"].as<int>();
            if (item["long_press_ms"].is<int>()) out["long_press_ms"] = item["long_press_ms"].as<int>();
            if (item["multi_press_window_ms"].is<int>()) out["multi_press_window_ms"] = item["multi_press_window_ms"].as<int>();
            if (item["repeat_start_ms"].is<int>()) out["repeat_start_ms"] = item["repeat_start_ms"].as<int>();
            if (item["repeat_interval_ms"].is<int>()) out["repeat_interval_ms"] = item["repeat_interval_ms"].as<int>();
        }
    }

    if (source["analog"].is<JsonArrayConst>()) {
        for (JsonObjectConst item : source["analog"].as<JsonArrayConst>()) {
            auto out = analog.add<JsonObject>();
            out["id"] = item["id"] | String("");
            out["pin"] = item["pin"] | 0;
            out["inverted"] = item["inverted"] | false;
            if (item["min_delta"].is<int>()) out["min_delta"] = item["min_delta"].as<int>();
            if (item["threshold_enabled"].is<bool>()) out["threshold_enabled"] = item["threshold_enabled"].as<bool>();
            if (item["threshold"].is<int>()) out["threshold"] = item["threshold"].as<int>();
            if (item["threshold_hysteresis"].is<int>()) out["threshold_hysteresis"] = item["threshold_hysteresis"].as<int>();
            if (item["range_enabled"].is<bool>()) out["range_enabled"] = item["range_enabled"].as<bool>();
            if (item["range_min"].is<int>()) out["range_min"] = item["range_min"].as<int>();
            if (item["range_max"].is<int>()) out["range_max"] = item["range_max"].as<int>();
            if (item["poll_interval_ms"].is<int>()) out["poll_interval_ms"] = item["poll_interval_ms"].as<int>();
        }
    }

    if (!StorageManager::instance().writeJson(OF_INPUTS_PATH, toSave)) {
        sendError(request, 500, "Failed to save inputs");
        return;
    }

    JsonDocument response;
    response["ok"] = true;
    response["restartRequired"] = true;
    response["message"] = "Inputs saved. Restarting to apply.";
    sendJson(request, response);
    scheduleRestart();
}

void ApiServer::handleOutputsUpdate(AsyncWebServerRequest* request, const String& body) {
    JsonDocument source;
    if (deserializeJson(source, body)) {
        sendError(request, 400, "Invalid JSON");
        return;
    }

    JsonDocument toSave;
    auto outputs = toSave["outputs"].to<JsonArray>();

    if (source["outputs"].is<JsonArrayConst>()) {
        for (JsonObjectConst item : source["outputs"].as<JsonArrayConst>()) {
            auto out = outputs.add<JsonObject>();
            out["id"] = item["id"] | String("");
            out["type"] = item["type"] | String("led");
            out["pin"] = item["pin"] | 0;
            out["inverted"] = item["inverted"] | false;
            out["pwm"] = item["pwm"] | false;

            if (item["pwm_channel"].is<int>()) out["pwm_channel"] = item["pwm_channel"].as<int>();
            if (item["pwm_frequency"].is<int>()) out["pwm_frequency"] = item["pwm_frequency"].as<int>();
            if (item["pwm_resolution"].is<int>()) out["pwm_resolution"] = item["pwm_resolution"].as<int>();

            if (item["pin_r"].is<int>()) out["pin_r"] = item["pin_r"].as<int>();
            if (item["pin_g"].is<int>()) out["pin_g"] = item["pin_g"].as<int>();
            if (item["pin_b"].is<int>()) out["pin_b"] = item["pin_b"].as<int>();
            if (item["channel_r"].is<int>()) out["channel_r"] = item["channel_r"].as<int>();
            if (item["channel_g"].is<int>()) out["channel_g"] = item["channel_g"].as<int>();
            if (item["channel_b"].is<int>()) out["channel_b"] = item["channel_b"].as<int>();

            if (item["led_count"].is<int>()) out["led_count"] = item["led_count"].as<int>();
            if (item["brightness"].is<int>()) out["brightness"] = item["brightness"].as<int>();
        }
    }

    if (!StorageManager::instance().writeJson(OF_OUTPUTS_PATH, toSave)) {
        sendError(request, 500, "Failed to save outputs");
        return;
    }

    JsonDocument response;
    response["ok"] = true;
    response["restartRequired"] = true;
    response["message"] = "Outputs saved. Restarting to apply.";
    sendJson(request, response);
    scheduleRestart();
}

void ApiServer::handleSensorsUpdate(AsyncWebServerRequest* request, const String& body) {
    JsonDocument source;
    if (deserializeJson(source, body)) {
        sendError(request, 400, "Invalid JSON");
        return;
    }

    JsonDocument toSave;
    auto sensors = toSave["sensors"].to<JsonArray>();

    if (source["sensors"].is<JsonArrayConst>()) {
        for (JsonObjectConst item : source["sensors"].as<JsonArrayConst>()) {
            auto out = sensors.add<JsonObject>();
            out["id"] = item["id"] | String("");
            out["type"] = item["type"] | String("");
            out["enabled"] = item["enabled"] | true;
            out["poll_interval_ms"] = item["poll_interval_ms"] | 5000;
            out["variable_prefix"] = item["variable_prefix"] | String("");
            out["address"] = item["address"] | 0x76;
            out["pin"] = item["pin"] | 0;

            if (item["temperature_offset_c"].is<float>()) out["temperature_offset_c"] = item["temperature_offset_c"].as<float>();
            if (item["sea_level_pressure_hpa"].is<float>()) out["sea_level_pressure_hpa"] = item["sea_level_pressure_hpa"].as<float>();
        }
    }

    if (!StorageManager::instance().writeJson(OF_SENSORS_PATH, toSave)) {
        sendError(request, 500, "Failed to save sensors");
        return;
    }

    JsonDocument response;
    response["ok"] = true;
    response["restartRequired"] = true;
    response["message"] = "Sensors saved. Restarting to apply.";
    sendJson(request, response);
    scheduleRestart();
}

void ApiServer::handleDisplaysUpdate(AsyncWebServerRequest* request, const String& body) {
    JsonDocument source;
    if (deserializeJson(source, body)) {
        sendError(request, 400, "Invalid JSON");
        return;
    }

    bool changed = false;

    if (source["displays"].is<JsonArrayConst>()) {
        JsonDocument cfgDoc;
        auto displays = cfgDoc["displays"].to<JsonArray>();
        for (JsonObjectConst item : source["displays"].as<JsonArrayConst>()) {
            auto out = displays.add<JsonObject>();
            out["id"] = item["id"] | String("");
            out["type"] = item["type"] | String("ssd1306");
            out["enabled"] = item["enabled"] | true;
            out["width"] = item["width"] | 128;
            out["height"] = item["height"] | 64;
            out["address"] = item["address"] | 0x3C;

            if (item["rotation"].is<int>()) out["rotation"] = item["rotation"].as<int>();
            if (item["refresh_interval_ms"].is<int>()) out["refresh_interval_ms"] = item["refresh_interval_ms"].as<int>();
            if (item["page"].is<const char*>()) out["page"] = item["page"].as<const char*>();
            if (item["reset_pin"].is<int>()) out["reset_pin"] = item["reset_pin"].as<int>();
            if (item["contrast"].is<int>()) out["contrast"] = item["contrast"].as<int>();
            if (item["cs_pin"].is<int>()) out["cs_pin"] = item["cs_pin"].as<int>();
            if (item["dc_pin"].is<int>()) out["dc_pin"] = item["dc_pin"].as<int>();
            if (item["mosi_pin"].is<int>()) out["mosi_pin"] = item["mosi_pin"].as<int>();
            if (item["sck_pin"].is<int>()) out["sck_pin"] = item["sck_pin"].as<int>();
            if (item["spi_frequency"].is<int>()) out["spi_frequency"] = item["spi_frequency"].as<int>();
            if (item["rx_pin"].is<int>()) out["rx_pin"] = item["rx_pin"].as<int>();
            if (item["tx_pin"].is<int>()) out["tx_pin"] = item["tx_pin"].as<int>();
            if (item["baud_rate"].is<int>()) out["baud_rate"] = item["baud_rate"].as<int>();
            if (item["uart_num"].is<int>()) out["uart_num"] = item["uart_num"].as<int>();
        }

        if (!StorageManager::instance().writeJson(OF_DISPLAYS_PATH, cfgDoc)) {
            sendError(request, 500, "Failed to save displays");
            return;
        }
        changed = true;
    }

    if (source["pages"].is<JsonArrayConst>()) {
        StorageManager::instance().mkdirs(OF_DISPLAY_PAGES_PATH);
        for (JsonObjectConst item : source["pages"].as<JsonArrayConst>()) {
            const String pageId = item["id"] | String("");
            if (!pageId.length()) continue;

            JsonDocument pageDoc;
            pageDoc["id"] = pageId;
            pageDoc["title"] = item["title"] | pageId;
            pageDoc["display_id"] = item["displayId"] | String("");

            auto widgets = pageDoc["widgets"].to<JsonArray>();
            if (item["widgets"].is<JsonArrayConst>()) {
                for (JsonObjectConst w : item["widgets"].as<JsonArrayConst>()) {
                    auto out = widgets.add<JsonObject>();
                    out["id"] = w["id"] | String("");
                    out["type"] = w["type"] | String("text");
                    out["x"] = w["x"] | 0;
                    out["y"] = w["y"] | 0;
                    out["text_size"] = w["textSize"] | 1;
                    out["text"] = w["text"] | String("");
                    out["variable"] = w["variableId"] | String("");
                    out["prefix"] = w["prefix"] | String("");
                    out["suffix"] = w["suffix"] | String("");
                    out["decimals"] = w["decimals"] | 1;
                    out["max_chars"] = w["maxChars"] | 0;
                }
            }

            const String path = String(OF_DISPLAY_PAGES_PATH) + "/" + pageId + ".json";
            if (!StorageManager::instance().writeJson(path, pageDoc)) {
                sendError(request, 500, "Failed to save display page " + pageId);
                return;
            }
        }
        changed = true;
    }

    if (!changed) {
        sendError(request, 400, "Payload must include displays or pages");
        return;
    }

    JsonDocument response;
    response["ok"] = true;
    response["restartRequired"] = true;
    response["message"] = "Displays/pages saved. Restarting to apply.";
    sendJson(request, response);
    scheduleRestart();
}

void ApiServer::sendModules(AsyncWebServerRequest* request) const {
    JsonDocument doc;
    auto arr = doc["modules"].to<JsonArray>();
    for (const auto& m : ModuleManager::instance().modules()) {
        auto obj = arr.add<JsonObject>();
        obj["address"] = m.address;
        obj["type"] = m.typeLabel;
        obj["online"] = m.online;
        obj["last_seen_ms"] = m.lastSeenMs;
    }
    doc["count"] = ModuleManager::instance().modules().size();
    sendJson(request, doc);
}

void ApiServer::sendActions(AsyncWebServerRequest* request) const {
    JsonDocument doc;
    if (!StorageManager::instance().readJson(OF_ACTIONS_PATH, doc)) {
        doc["actions"].to<JsonArray>();
    } else if (!doc["actions"].is<JsonArrayConst>()) {
        doc["actions"].to<JsonArray>();
    }
    doc["count"] = doc["actions"].is<JsonArrayConst>() ? doc["actions"].as<JsonArrayConst>().size() : 0;

    auto histArr = doc["history"].to<JsonArray>();
    const auto& hist = ActionEngine::instance().history();
    const size_t start = hist.size() > 100 ? hist.size() - 100 : 0;
    for (size_t i = start; i < hist.size(); ++i) {
        auto hObj = histArr.add<JsonObject>();
        hObj["action_id"] = hist[i].actionId;
        hObj["action_name"] = hist[i].actionName;
        hObj["timestamp_ms"] = hist[i].timestampMs;
        hObj["success"] = hist[i].success;
        if (!hist[i].error.isEmpty()) hObj["error"] = hist[i].error;
    }
    sendJson(request, doc);
}

void ApiServer::sendMacros(AsyncWebServerRequest* request) const {
    JsonDocument doc;
    auto arr = doc["macros"].to<JsonArray>();
    for (const auto& macro : MacroManager::instance().macros()) {
        auto obj = arr.add<JsonObject>();
        obj["id"] = macro.id;
        obj["name"] = macro.name;
        obj["enabled"] = macro.enabled;
        auto acts = obj["action_ids"].to<JsonArray>();
        for (const auto& id : macro.actionIds) acts.add(id);
    }
    doc["count"] = MacroManager::instance().macros().size();
    sendJson(request, doc);
}

void ApiServer::handleActionsUpdate(AsyncWebServerRequest* request, const String& body) {
    JsonDocument doc;
    if (deserializeJson(doc, body)) {
        sendError(request, 400, "Invalid JSON");
        return;
    }

    auto parseConditionOp = [](const String& op) -> ConditionOp {
        if (op == "ne") return ConditionOp::Ne;
        if (op == "lt") return ConditionOp::Lt;
        if (op == "lte") return ConditionOp::Lte;
        if (op == "gt") return ConditionOp::Gt;
        if (op == "gte") return ConditionOp::Gte;
        return ConditionOp::Eq;
    };

    auto parseActionType = [](const String& typeStr) -> ActionType {
        if (typeStr == "http_request") return ActionType::HttpRequest;
        if (typeStr == "mqtt_publish") return ActionType::MqttPublish;
        if (typeStr == "variable_set") return ActionType::VariableSet;
        if (typeStr == "variable_increment") return ActionType::VariableIncrement;
        if (typeStr == "variable_toggle") return ActionType::VariableToggle;
        if (typeStr == "ha_service_call") return ActionType::HaServiceCall;
        if (typeStr == "page_change") return ActionType::PageChange;
        if (typeStr == "notification") return ActionType::Notification;
        if (typeStr == "keyboard_shortcut") return ActionType::KeyboardShortcut;
        if (typeStr == "media_control") return ActionType::MediaControl;
        return ActionType::Delay;
    };

    auto parseActionObject = [&](JsonObjectConst item, ActionConfig& action) {
        action.id = item["id"] | String("");
        action.name = item["name"] | String("");
        action.enabled = item["enabled"] | true;

        if (item["conditions"].is<JsonArrayConst>()) {
            for (JsonObjectConst cond : item["conditions"].as<JsonArrayConst>()) {
                Condition c;
                c.variableId = cond["variable_id"] | String("");
                c.op = parseConditionOp(cond["op"] | String("eq"));
                c.value = cond["value"] | String("");
                if (c.variableId.length()) action.conditions.push_back(c);
            }
        }

        if (item["steps"].is<JsonArrayConst>()) {
            for (JsonObjectConst step : item["steps"].as<JsonArrayConst>()) {
                ActionStep s;
                s.type = parseActionType(step["type"] | String("delay"));
                s.delayMs = step["delay_ms"] | 0;
                s.url = step["url"] | String("");
                s.method = step["method"] | String("GET");
                s.body = step["body"] | String("");
                s.topic = step["topic"] | String("");
                s.variableId = step["variable_id"] | String("");
                s.value = step["value"] | String("");
                s.increment = step["increment"] | 1.0f;
                s.haService = step["ha_service"] | String("");
                s.haEntityId = step["ha_entity_id"] | String("");
                s.displayId = step["display_id"] | String("");
                s.pageId = step["page_id"] | String("");
                s.message = step["message"] | String("");
                s.keysCombo = step["keys"] | String("");
                s.mediaCommand = step["media_command"] | String("");
                action.steps.push_back(s);
            }
        }
    };

    bool updated = false;
    if (doc["actions"].is<JsonArrayConst>()) {
        for (JsonObjectConst item : doc["actions"].as<JsonArrayConst>()) {
            ActionConfig action;
            parseActionObject(item, action);
            if (!action.id.length()) continue;
            ActionEngine::instance().registerAction(action);
            updated = true;
        }
    } else if (doc["id"].is<const char*>()) {
        ActionConfig action;
        parseActionObject(doc.as<JsonObjectConst>(), action);
        if (action.id.length()) {
            ActionEngine::instance().registerAction(action);
            updated = true;
        }
    }

    if (updated) {
        if (!ActionEngine::instance().saveActions()) {
            sendError(request, 500, "Failed to save actions");
            return;
        }
    }
    sendActions(request);
}

void ApiServer::handleMacrosUpdate(AsyncWebServerRequest* request, const String& body) {
    JsonDocument doc;
    if (deserializeJson(doc, body)) {
        sendError(request, 400, "Invalid JSON");
        return;
    }

    auto parseMacroObject = [&](JsonObjectConst item, MacroConfig& macro) {
        macro.id = item["id"] | String("");
        macro.name = item["name"] | String("");
        macro.enabled = item["enabled"] | true;
        if (item["action_ids"].is<JsonArrayConst>()) {
            for (JsonVariantConst v : item["action_ids"].as<JsonArrayConst>()) {
                const String id = v | String("");
                if (id.length()) macro.actionIds.push_back(id);
            }
        }
    };

    bool updated = false;
    if (doc["macros"].is<JsonArrayConst>()) {
        for (JsonObjectConst item : doc["macros"].as<JsonArrayConst>()) {
            MacroConfig macro;
            parseMacroObject(item, macro);
            if (!macro.id.length()) continue;
            MacroManager::instance().registerMacro(macro);
            updated = true;
        }
    } else if (doc["id"].is<const char*>()) {
        MacroConfig macro;
        parseMacroObject(doc.as<JsonObjectConst>(), macro);
        if (macro.id.length()) {
            MacroManager::instance().registerMacro(macro);
            updated = true;
        }
    }

    if (updated) {
        if (!MacroManager::instance().saveMacros()) {
            sendError(request, 500, "Failed to save macros");
            return;
        }
    }
    sendMacros(request);
}

// ── Profile handlers ──────────────────────────────────────────────────────────

void ApiServer::sendProfiles(AsyncWebServerRequest* request) const {
    JsonDocument doc;
    doc["activeId"] = ProfileManager::instance().activeId();
    auto arr = doc["profiles"].to<JsonArray>();
    for (const auto& p : ProfileManager::instance().profiles()) {
        auto obj = arr.add<JsonObject>();
        obj["id"]        = p.id;
        obj["name"]      = p.name;
        obj["createdMs"] = p.createdMs;
        obj["active"]    = (p.id == ProfileManager::instance().activeId());
    }
    doc["count"] = ProfileManager::instance().profiles().size();
    sendJson(request, doc);
}

void ApiServer::handleProfileCreate(AsyncWebServerRequest* request, const String& body) {
    JsonDocument doc;
    if (deserializeJson(doc, body)) {
        sendError(request, 400, "Invalid JSON");
        return;
    }

    const String name = doc["name"] | String("");
    if (!name.length()) {
        sendError(request, 400, "Profile name is required");
        return;
    }

    String outId;
    if (!ProfileManager::instance().create(name, outId)) {
        sendError(request, 500, "Failed to create profile");
        return;
    }

    sendProfiles(request);
}

void ApiServer::handleProfileActivate(AsyncWebServerRequest* request, const String& body) {
    JsonDocument doc;
    if (deserializeJson(doc, body)) {
        sendError(request, 400, "Invalid JSON");
        return;
    }

    const String id = doc["id"] | String("");
    if (!id.length()) {
        sendError(request, 400, "Profile id is required");
        return;
    }

    String error;
    if (!ProfileManager::instance().activate(id, error)) {
        sendError(request, 400, error);
        return;
    }

    publishHealthUpdate();
    sendProfiles(request);
}

void ApiServer::handleProfileDelete(AsyncWebServerRequest* request, const String& profileId) {
    if (!profileId.length()) {
        sendError(request, 400, "Profile id is required");
        return;
    }

    String error;
    if (!ProfileManager::instance().remove(profileId, error)) {
        sendError(request, 400, error);
        return;
    }

    sendProfiles(request);
}

// ── Template handlers ─────────────────────────────────────────────────────────

void ApiServer::sendTemplates(AsyncWebServerRequest* request) const {
    JsonDocument doc;
    auto arr = doc["templates"].to<JsonArray>();

    const auto files = StorageManager::instance().listDir("/templates");
    for (const auto& f : files) {
        if (!f.endsWith(".json")) continue;
        JsonDocument tmpl;
        if (!StorageManager::instance().readJson(f, tmpl)) continue;
        auto obj = arr.add<JsonObject>();
        obj["id"]        = tmpl["id"] | String("");
        obj["name"]      = tmpl["name"] | String("");
        obj["createdMs"] = tmpl["createdMs"] | 0;
    }

    doc["count"] = arr.size();
    sendJson(request, doc);
}

void ApiServer::sendTemplateById(AsyncWebServerRequest* request, const String& templateId) const {
    JsonDocument tmpl;
    const String path = "/templates/" + templateId + ".json";
    if (!StorageManager::instance().readJson(path, tmpl)) {
        sendError(request, 404, "Template not found");
        return;
    }
    sendJson(request, tmpl);
}

void ApiServer::handleTemplateExport(AsyncWebServerRequest* request, const String& body) {
    JsonDocument doc;
    if (deserializeJson(doc, body)) {
        sendError(request, 400, "Invalid JSON");
        return;
    }

    const String name = doc["name"] | String("Template");

    // Generate a unique ID
    char idBuf[32];
    snprintf(idBuf, sizeof(idBuf), "tmpl_%08lx", static_cast<unsigned long>(millis()));
    const String id = String(idBuf);

    JsonDocument tmplDoc;
    tmplDoc["id"]        = id;
    tmplDoc["name"]      = name;
    tmplDoc["createdMs"] = millis();

    // Embed device config (minus Wi-Fi credentials for security)
    JsonDocument cfgDoc;
    ConfigManager::instance().toJson(cfgDoc);
    cfgDoc.remove("wifi");
    tmplDoc["config"].set(cfgDoc);

    // Embed current device state
    JsonDocument stateDoc;
    ProfileManager::instance().captureState(stateDoc);
    tmplDoc["state"].set(stateDoc);

    StorageManager::instance().mkdirs("/templates");
    const String path = "/templates/" + id + ".json";
    if (!StorageManager::instance().writeJson(path, tmplDoc)) {
        sendError(request, 500, "Failed to write template");
        return;
    }

    JsonDocument response;
    response["ok"]       = true;
    response["id"]       = id;
    response["name"]     = name;
    sendJson(request, response);
}

void ApiServer::handleTemplateImport(AsyncWebServerRequest* request, const String& body) {
    JsonDocument tmplDoc;
    if (deserializeJson(tmplDoc, body)) {
        sendError(request, 400, "Invalid JSON");
        return;
    }

    const JsonVariantConst stateVariant = tmplDoc["state"];
    if (!stateVariant.is<JsonObjectConst>()) {
        sendError(request, 400, "Template has no state data");
        return;
    }

    JsonDocument stateDoc;
    stateDoc.set(stateVariant);
    String error;
    if (!ProfileManager::instance().applyState(stateDoc, error)) {
        sendError(request, 500, error);
        return;
    }

    publishHealthUpdate();

    JsonDocument response;
    response["ok"] = true;
    sendJson(request, response);
}

void ApiServer::sendNotifications(AsyncWebServerRequest* request) const {
    JsonDocument doc;
    auto arr = doc["notifications"].to<JsonArray>();
    for (const auto& n : NotificationManager::instance().notifications()) {
        auto obj = arr.add<JsonObject>();
        obj["id"]          = n.id;
        obj["type"]        = n.type;
        obj["message"]     = n.message;
        obj["timestampMs"] = n.timestampMs;
        obj["read"]        = n.read;
    }
    doc["unreadCount"] = NotificationManager::instance().unreadCount();
    sendJson(request, doc);
}
