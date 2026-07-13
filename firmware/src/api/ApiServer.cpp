#include "ApiServer.h"

#include <ArduinoJson.h>
#include <LittleFS.h>
#include <Wire.h>
#include <functional>
#include <algorithm>
#include <vector>
#include "../core/PlatformCompat.h"
#include "../OpenFrameConfig.h"
#include "../core/StorageManager.h"
#include "../core/ConfigManager.h"
#include "../core/Lock.h"
#include "../hardware/InputManager.h"
#include "../hardware/OutputManager.h"
#include "../hardware/SensorManager.h"
#include "../hardware/DisplayManager.h"
#include "../hardware/TouchManager.h"
#include "../hardware/ModuleManager.h"
#include "../hardware/HardwareInfo.h"
#include "../hardware/SdCard.h"
#include "../managers/ActionEngine.h"
#include "../managers/MacroManager.h"
#include "../managers/SceneManager.h"
#include "../managers/OtaManager.h"
#include "../managers/VariableManager.h"
#include "../managers/WiFiManager.h"
#include "../managers/MqttManager.h"
#include "../managers/ProfileManager.h"
#include "../managers/TimeManager.h"
#include "../managers/NodeLink.h"
#include "../managers/HealthMonitor.h"
#include "../managers/NotificationManager.h"
#include "../managers/PushNotifier.h"     // self-gated on OF_ENABLE_PUSH
#include "../managers/WeatherManager.h"   // self-gated on OF_ENABLE_WEATHER
#include "../managers/ConfigHotApply.h"

// Constant-time token compare (defined below; forward-declared so the WS auth
// handshake in handleWebSocketMessage can use it before its definition).
static bool tokenEquals(const String& a, const String& b);

namespace {

// Maximum accepted POST/WS payload. Bodies above this are rejected before they
// can exhaust the heap on a memory-constrained device.
constexpr size_t MAX_POST_PAYLOAD = OF_MAX_POST_PAYLOAD;

// Free the accumulated body buffer stashed in request->_tempObject. The library
// destructor only free()s _tempObject (it never runs the String destructor), so
// we must delete it ourselves — both on normal completion and, via onDisconnect,
// when the client aborts mid-upload (otherwise the String's heap buffer leaks).
void freeTempStringBody(AsyncWebServerRequest* request) {
    if (request->_tempObject) {
        delete static_cast<String*>(request->_tempObject);
        request->_tempObject = nullptr;
    }
}

using JsonBodyHandler = std::function<void(AsyncWebServerRequest*, const String&)>;

// Register an HTTP_POST route that accumulates the request body (with a hard
// size cap) and invokes `handler` once the full body has arrived. Centralizes
// the body-buffering boilerplate and guarantees the temp buffer is freed.
void registerJsonPost(AsyncWebServer& server, const char* path, JsonBodyHandler handler) {
    server.on(path, HTTP_POST,
        [](AsyncWebServerRequest*) {},
        nullptr,
        [handler](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
            // Oversized: don't buffer; respond 413 once the stream completes.
            if (total > MAX_POST_PAYLOAD) {
                if (index + len >= total) {
                    request->send(413, "application/json",
                                  "{\"error\":\"Payload too large\"}");
                }
                return;
            }

            String* body = index == 0 ? new String() : static_cast<String*>(request->_tempObject);
            if (!body) return;
            if (index == 0) {
                body->reserve(total);
                request->_tempObject = body;
                request->onDisconnect([request]() { freeTempStringBody(request); });
            }
            body->concat(reinterpret_cast<const char*>(data), len);

            if (index + len == total) {
                if (ApiServer::instance().requireAuth(request)) {
                    handler(request, *body);
                }
                freeTempStringBody(request);
            }
        });
}

// Validate a user-supplied filesystem path for the FS browser API. Rejects
// directory traversal, empty/over-long paths, and non-printable characters.
bool isSafeFsPath(const String& path) {
    if (path.length() == 0 || path.length() > 128) return false;
    if (path[0] != '/') return false;
    if (path.indexOf("..") >= 0) return false;
    if (path.indexOf("//") >= 0) return false;
    // Reject directory paths (trailing slash) so upload/delete never target a dir.
    if (path.length() > 1 && path.endsWith("/")) return false;
    for (size_t i = 0; i < path.length(); ++i) {
        const char c = path[i];
        if (c < 0x20 || c > 0x7E) return false;
    }
    return true;
}

// Paths the generic FS API must never modify or delete. Web UI assets are owned
// by the OTA/filesystem image; the active config has its own dedicated endpoint.
bool isProtectedFsPath(const String& path) {
    return path == "/www" || path.startsWith("/www/");
}

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
    obj["readOnly"] = var.readOnly;
    // Source category for the Variables tab / designer (F3) — derived from the id
    // namespace so the frontend doesn't have to string-parse prefixes.
    const char* source = "local";
    if      (var.id.startsWith("sensor."))  source = "sensor";
    else if (var.id.startsWith("input."))   source = "input";
    else if (var.id.startsWith("output."))  source = "output";
    else if (var.id.startsWith("node."))    source = "node";
    else if (var.id.startsWith("weather.")) source = "weather";
    obj["source"] = source;
    if (var.hasRange) { obj["min"] = var.rangeMin; obj["max"] = var.rangeMax; }
    if (var.unit.length()) obj["unit"] = var.unit;
    if (!var.options.empty()) {
        auto opts = obj["options"].to<JsonArray>();
        for (const auto& o : var.options) opts.add(o);
    }

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
    // Reserve the exact size up front: serializeJson() into a String otherwise
    // grows it by repeated reallocation, churning/fragmenting the heap on every
    // response and WebSocket frame.
    body.reserve(measureJson(doc) + 1);
    serializeJson(doc, body);
    return body;
}

const char* contentTypeForPath(const String& path) {
    if (path.endsWith(".html")) return "text/html";
    if (path.endsWith(".css")) return "text/css";
    if (path.endsWith(".js")) return "application/javascript";
    if (path.endsWith(".json")) return "application/json";
    if (path.endsWith(".webmanifest")) return "application/manifest+json";
    if (path.endsWith(".txt")) return "text/plain";
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
    // Reject directory traversal before touching the filesystem.
    if (path.indexOf("..") >= 0) {
        request->send(400, "text/plain", "Bad request");
        return;
    }

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
    response->addHeader("X-Content-Type-Options", "nosniff");
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
            // API + hashed assets have their own routes; a miss here is a real 404.
            if (url.startsWith("/api/") || url.startsWith("/assets/")) {
                request->send(404, "text/plain", "Not found");
                return;
            }
            // A dotted path is a file request (manifest.webmanifest, sw.js, PWA
            // icons, robots.txt) — serve it from /www if present, else 404.
            // sendStaticFile handles the .gz fallback and 404s on its own.
            if (url.indexOf('.') >= 0) {
                sendStaticFile(request, "/www" + url, "public, max-age=86400");
                return;
            }
            // Extensionless path — an SPA route; hand back the app shell.
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

    // Prometheus exposition endpoint (#84) — plain-text telemetry, left open like
    // the other read-only status endpoints so scrapers don't need the API token.
    server.on("/metrics", HTTP_GET, [](AsyncWebServerRequest* request) {
        ApiServer::instance().sendMetrics(request);
    });

    // Boot/system self-test (#9): RAM, flash, filesystem, I2C bus scan.
    server.on("/api/selftest", HTTP_GET, [](AsyncWebServerRequest* request) {
        ApiServer::instance().sendSelfTest(request);
    });

    // Network diagnostics (#86): WiFi, DNS resolve timing, broker reachability, NTP.
    server.on("/api/netdiag", HTTP_GET, [](AsyncWebServerRequest* request) {
        ApiServer::instance().sendNetDiag(request);
    });

    // Config backup/restore — sub-routes registered BEFORE "/api/config" so the
    // parent GET handler doesn't swallow them (see canHandle subpath note above).
    server.on("/api/config/backups", HTTP_GET, [](AsyncWebServerRequest* request) {
        if (!ApiServer::instance().requireAuth(request)) return;
        ApiServer::instance().sendConfigBackups(request);
    });
    server.on("/api/config/backup", HTTP_POST, [](AsyncWebServerRequest* request) {
        if (!ApiServer::instance().requireAuth(request)) return;
        ApiServer::instance().handleConfigBackupCreate(request);
    });
    server.on("/api/factory-reset", HTTP_POST, [](AsyncWebServerRequest* request) {
        if (!ApiServer::instance().requireAuth(request)) return;
        // Snapshot first (so even a reset is recoverable), then wipe keeping WiFi.
        ApiServer::instance().createConfigBackup();
        ConfigManager::instance().factoryResetKeepWifi();
        JsonDocument doc;
        doc["ok"] = true;
        doc["message"] = "Factory reset (WiFi kept). Restarting.";
        ApiServer::instance().sendJson(request, doc);
        ApiServer::instance().scheduleRestart();
    });
    registerJsonPost(server, "/api/config/restore", [](AsyncWebServerRequest* request, const String& body) {
        ApiServer::instance().handleConfigRestore(request, body);
    });
    server.on("/api/config/backups/*", HTTP_DELETE, [](AsyncWebServerRequest* request) {
        if (!ApiServer::instance().requireAuth(request)) return;
        ApiServer::instance().handleConfigBackupDelete(request);
    });

    server.on("/api/config", HTTP_GET, [](AsyncWebServerRequest* request) {
        // Config carries WiFi/MQTT secrets — gate it even though it's a GET.
        if (!ApiServer::instance().requireAuth(request)) return;
        ApiServer::instance().sendConfig(request);
    });
    server.on("/api/wifi/scan", HTTP_GET, [](AsyncWebServerRequest* request) {
        ApiServer::instance().sendWifiScan(request);
    });
    registerJsonPost(server, "/api/config", [](AsyncWebServerRequest* request, const String& body) {
        ApiServer::instance().handleConfigUpdate(request, body);
    });

    server.on("/api/variables", HTTP_GET, [](AsyncWebServerRequest* request) {
        ApiServer::instance().sendVariables(request);
    });
    registerJsonPost(server, "/api/variables", [](AsyncWebServerRequest* request, const String& body) {
        ApiServer::instance().handleVariablesUpdate(request, body);
    });

    server.on("/api/logs", HTTP_GET, [](AsyncWebServerRequest* request) {
        ApiServer::instance().sendLogs(request);
    });

    server.on("/api/crashlog", HTTP_GET, [](AsyncWebServerRequest* request) {
        JsonDocument doc;
        if (!StorageManager::instance().readJson(OF_CRASHLOG_PATH, doc)) {
            doc["resets"].to<JsonArray>();
        }
        ApiServer::instance().sendJson(request, doc);
    });

    server.on("/api/ota/check", HTTP_GET, [](AsyncWebServerRequest* request) {
        ApiServer::instance().sendOtaStatus(request);
    });

    server.on("/api/inputs", HTTP_GET, [](AsyncWebServerRequest* request) {
        ApiServer::instance().sendInputs(request);
    });
    registerJsonPost(server, "/api/inputs", [](AsyncWebServerRequest* request, const String& body) {
        ApiServer::instance().handleInputsUpdate(request, body);
    });

    // NOTE: ESPAsyncWebServer matches a handler registered for "/api/outputs"
    // against any URL starting with "/api/outputs/" (WebHandlerImpl canHandle),
    // and the first registered handler wins. So sub-routes MUST be registered
    // before their parent, or the parent swallows them (e.g. /api/outputs/control
    // would hit handleOutputsUpdate and wipe the config). Children first:
    server.on("/api/outputs/state", HTTP_GET, [](AsyncWebServerRequest* request) {
        ApiServer::instance().sendOutputsState(request);
    });
    registerJsonPost(server, "/api/outputs/control", [](AsyncWebServerRequest* request, const String& body) {
        ApiServer::instance().handleOutputsControl(request, body);
    });
    server.on("/api/outputs", HTTP_GET, [](AsyncWebServerRequest* request) {
        ApiServer::instance().sendOutputs(request);
    });
    registerJsonPost(server, "/api/outputs", [](AsyncWebServerRequest* request, const String& body) {
        ApiServer::instance().handleOutputsUpdate(request, body);
    });

    server.on("/api/sensors", HTTP_GET, [](AsyncWebServerRequest* request) {
        ApiServer::instance().sendSensors(request);
    });
    registerJsonPost(server, "/api/sensors", [](AsyncWebServerRequest* request, const String& body) {
        ApiServer::instance().handleSensorsUpdate(request, body);
    });

    // Home Assistant entity import (HA → variable bus). GET returns the imported set;
    // POST replaces it and restarts to re-bind the bridge (like inputs/outputs).
    server.on("/api/ha/import", HTTP_GET, [](AsyncWebServerRequest* request) {
        ApiServer::instance().sendHaImport(request);
    });
    registerJsonPost(server, "/api/ha/import", [](AsyncWebServerRequest* request, const String& body) {
        ApiServer::instance().handleHaImportUpdate(request, body);
    });

    // Sub-route before parent (see note above).
    server.on("/api/displays/pages", HTTP_GET, [](AsyncWebServerRequest* request) {
        ApiServer::instance().sendDisplayPages(request);
    });
    server.on("/api/displays", HTTP_GET, [](AsyncWebServerRequest* request) {
        ApiServer::instance().sendDisplays(request);
    });
    registerJsonPost(server, "/api/displays", [](AsyncWebServerRequest* request, const String& body) {
        ApiServer::instance().handleDisplaysUpdate(request, body);
    });
    // Live screen content (geometry + active page + resolved widget text) so the
    // web UI can render a preview of what's on the glass without a framebuffer.
    server.on("/api/screens", HTTP_GET, [](AsyncWebServerRequest* request) {
        ApiServer::instance().sendScreens(request);
    });

#if defined(OF_ENABLE_XPT2046_TOUCH)
    // Resistive-touch calibration (boards that drive an XPT2046 directly). GET
    // returns the current calibration plus the last raw/mapped point for a live
    // four-corner UI; POST merges + persists a new calibration and hot-applies it.
    server.on("/api/touch", HTTP_GET, [](AsyncWebServerRequest* request) {
        ApiServer::instance().sendTouchCalibration(request);
    });
    registerJsonPost(server, "/api/touch", [](AsyncWebServerRequest* request, const String& body) {
        ApiServer::instance().handleTouchUpdate(request, body);
    });
#endif

    server.on("/api/modules", HTTP_GET, [](AsyncWebServerRequest* request) {
        ApiServer::instance().sendModules(request);
    });

    server.on("/api/hardware", HTTP_GET, [](AsyncWebServerRequest* request) {
        ApiServer::instance().sendHardware(request);
    });
    registerJsonPost(server, "/api/hardware/adopt", [](AsyncWebServerRequest* request, const String& body) {
        ApiServer::instance().handleHardwareAdopt(request, body);
    });

    server.on("/api/actions", HTTP_GET, [](AsyncWebServerRequest* request) {
        ApiServer::instance().sendActions(request);
    });
    // Sub-route before the "/api/actions" parent (see the canHandle subpath note).
    registerJsonPost(server, "/api/actions/simulate", [](AsyncWebServerRequest* request, const String& body) {
        ApiServer::instance().handleActionSimulate(request, body);
    });
    registerJsonPost(server, "/api/actions", [](AsyncWebServerRequest* request, const String& body) {
        ApiServer::instance().handleActionsUpdate(request, body);
    });
    server.on("/api/actions/*", HTTP_DELETE, [](AsyncWebServerRequest* request) {
        if (!ApiServer::instance().requireAuth(request)) return;
        const String url = request->url();
        const String actionId = url.substring(url.lastIndexOf('/') + 1);
        ApiServer::instance().handleActionDelete(request, actionId);
    });

    server.on("/api/macros", HTTP_GET, [](AsyncWebServerRequest* request) {
        ApiServer::instance().sendMacros(request);
    });
    registerJsonPost(server, "/api/macros", [](AsyncWebServerRequest* request, const String& body) {
        ApiServer::instance().handleMacrosUpdate(request, body);
    });
    server.on("/api/macros/*", HTTP_DELETE, [](AsyncWebServerRequest* request) {
        if (!ApiServer::instance().requireAuth(request)) return;
        const String url = request->url();
        const String macroId = url.substring(url.lastIndexOf('/') + 1);
        ApiServer::instance().handleMacroDelete(request, macroId);
    });

    // ── Scenes (#39) ────────────────────────────────────────────────────────────
    // Specific sub-routes before the "/api/scenes" parent + wildcard delete.
    registerJsonPost(server, "/api/scenes/capture", [](AsyncWebServerRequest* request, const String& body) {
        ApiServer::instance().handleSceneCapture(request, body);
    });
    registerJsonPost(server, "/api/scenes/restore", [](AsyncWebServerRequest* request, const String& body) {
        ApiServer::instance().handleSceneRestore(request, body);
    });
    server.on("/api/scenes", HTTP_GET, [](AsyncWebServerRequest* request) {
        ApiServer::instance().sendScenes(request);
    });
    server.on("/api/scenes/*", HTTP_DELETE, [](AsyncWebServerRequest* request) {
        if (!ApiServer::instance().requireAuth(request)) return;
        const String url = request->url();
        const String name = url.substring(url.lastIndexOf('/') + 1);
        ApiServer::instance().handleSceneDelete(request, name);
    });

    // ── Profiles ──────────────────────────────────────────────────────────────
    // Sub-route before parent: "/api/profiles" would otherwise swallow
    // "/api/profiles/activate" (see canHandle subpath note above).
    registerJsonPost(server, "/api/profiles/activate", [](AsyncWebServerRequest* request, const String& body) {
        ApiServer::instance().handleProfileActivate(request, body);
    });
    server.on("/api/profiles", HTTP_GET, [](AsyncWebServerRequest* request) {
        ApiServer::instance().sendProfiles(request);
    });
    registerJsonPost(server, "/api/profiles", [](AsyncWebServerRequest* request, const String& body) {
        ApiServer::instance().handleProfileCreate(request, body);
    });
    server.on("/api/profiles/*", HTTP_DELETE, [](AsyncWebServerRequest* request) {
        if (!ApiServer::instance().requireAuth(request)) return;
        const String url = request->url();
        const String profileId = url.substring(url.lastIndexOf('/') + 1);
        ApiServer::instance().handleProfileDelete(request, profileId);
    });

    // ── Templates ─────────────────────────────────────────────────────────────
    // Sub-route GET before parent GET: "/api/templates" would otherwise swallow
    // "/api/templates/<id>" (see canHandle subpath note above).
    server.on("/api/templates/*", HTTP_GET, [](AsyncWebServerRequest* request) {
        const String url = request->url();
        const String templateId = url.substring(url.lastIndexOf('/') + 1);
        ApiServer::instance().sendTemplateById(request, templateId);
    });
    server.on("/api/templates", HTTP_GET, [](AsyncWebServerRequest* request) {
        ApiServer::instance().sendTemplates(request);
    });
    registerJsonPost(server, "/api/templates/export", [](AsyncWebServerRequest* request, const String& body) {
        ApiServer::instance().handleTemplateExport(request, body);
    });
    registerJsonPost(server, "/api/templates/import", [](AsyncWebServerRequest* request, const String& body) {
        ApiServer::instance().handleTemplateImport(request, body);
    });
    server.on("/api/templates/*", HTTP_DELETE, [](AsyncWebServerRequest* request) {
        if (!ApiServer::instance().requireAuth(request)) return;
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
#if OF_ENABLE_PUSH
    // Test push. Validation happens here, but the HTTP POST itself is queued and
    // sent from the loop task (outbound HTTP must never run on the async callback
    // task) — so "ok" means "queued", with delivery failures visible in the logs.
    registerJsonPost(server, "/api/notify/test", [](AsyncWebServerRequest* request, const String&) {
        JsonDocument resp;
        String why;
        if (!PushNotifier::instance().validateConfig(why)) {
            resp["ok"]    = false;
            resp["error"] = why;
            String s;
            serializeJson(resp, s);
            request->send(422, "application/json", s);
            return;
        }
        PushNotifier::instance().send(ConfigManager::instance().config().device.name,
                                      "Test notification from OpenFrame", 3);
        resp["ok"]      = true;
        resp["message"] = "Test push queued";
        String s;
        serializeJson(resp, s);
        request->send(200, "application/json", s);
    });
#endif
    server.on("/api/notifications", HTTP_GET, [](AsyncWebServerRequest* request) {
        ApiServer::instance().sendNotifications(request);
    });
    registerJsonPost(server, "/api/notifications/read", [](AsyncWebServerRequest* request, const String& body) {
        JsonDocument doc;
        if (deserializeJson(doc, body) == DeserializationError::Ok && doc["id"].is<const char*>()) {
            NotificationManager::instance().markRead(doc["id"].as<String>());
        } else {
            NotificationManager::instance().markAllRead();
        }
        JsonDocument resp;
        resp["ok"] = true;
        String s;
        serializeJson(resp, s);
        request->send(200, "application/json", s);
    });

    // ── Filesystem browser ────────────────────────────────────────────────────
    // The whole FS browser is gated for read too: /api/fs/download can read any
    // file (incl. /config.json with its secrets), so leaving reads open while
    // gating writes would be a trivial auth bypass.
    server.on("/api/fs/stat", HTTP_GET, [](AsyncWebServerRequest* request) {
        if (!ApiServer::instance().requireAuth(request)) return;
        ApiServer::instance().sendFsStat(request);
    });
    server.on("/api/fs/selftest", HTTP_GET, [](AsyncWebServerRequest* request) {
        if (!ApiServer::instance().requireAuth(request)) return;
        ApiServer::instance().sendFsSelfTest(request);
    });
    server.on("/api/fs/list", HTTP_GET, [](AsyncWebServerRequest* request) {
        if (!ApiServer::instance().requireAuth(request)) return;
        ApiServer::instance().sendFsList(request);
    });
    server.on("/api/fs/download", HTTP_GET, [](AsyncWebServerRequest* request) {
        if (!ApiServer::instance().requireAuth(request)) return;
        ApiServer::instance().sendFsDownload(request);
    });
    server.on("/api/fs", HTTP_DELETE, [](AsyncWebServerRequest* request) {
        if (!ApiServer::instance().requireAuth(request)) return;
        ApiServer::instance().handleFsDelete(request);
    });
    server.on("/api/fs/mkdir", HTTP_POST, [](AsyncWebServerRequest* request) {
        if (!ApiServer::instance().requireAuth(request)) return;
        ApiServer::instance().handleFsMkdir(request);
    });
    registerJsonPost(server, "/api/fs/rename", [](AsyncWebServerRequest* request, const String& body) {
        ApiServer::instance().handleFsRename(request, body);
    });
    // Upload/replace a file. Target path comes from the ?path= query parameter.
    server.on("/api/fs", HTTP_POST,
        [](AsyncWebServerRequest*) {},
        nullptr,
        [](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
            if (total > MAX_POST_PAYLOAD) {
                if (index + len >= total) {
                    request->send(413, "application/json", "{\"error\":\"Payload too large\"}");
                }
                return;
            }
            String* body = index == 0 ? new String() : static_cast<String*>(request->_tempObject);
            if (!body) return;
            if (index == 0) {
                body->reserve(total);
                request->_tempObject = body;
                request->onDisconnect([request]() { freeTempStringBody(request); });
            }
            body->concat(reinterpret_cast<const char*>(data), len);
            if (index + len == total) {
                if (ApiServer::instance().requireAuth(request)) {
                    String path = request->hasParam("path") ? request->getParam("path")->value() : String("");
                    ApiServer::instance().handleFsUpload(request, path, *body);
                }
                freeTempStringBody(request);
            }
        });

    // ── Display images (background + sprite widgets) ──────────────────────────
    server.on("/api/images", HTTP_GET, [](AsyncWebServerRequest* request) {
        ApiServer::instance().sendImages(request);
    });
    server.on("/api/images", HTTP_DELETE, [](AsyncWebServerRequest* request) {
        ApiServer::instance().handleImageDelete(request);
    });
    // Streaming upload: the raw OFIM body is written straight to /images/<name> in
    // chunks, never buffered whole — a full-screen colour frame is ~150 KB.
    server.on("/api/images", HTTP_POST,
        [](AsyncWebServerRequest*) {},
        nullptr,
        [](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
            ApiServer::instance().handleImageUploadChunk(request, data, len, index, total);
        });
}

void ApiServer::registerWebSocket(AsyncWebServer& server) {
    _ws.onEvent([](AsyncWebSocket* ws, AsyncWebSocketClient* client, AwsEventType type, void* arg, uint8_t* data, size_t len) {
        (void)ws;

        if (type == WS_EVT_CONNECT) {
            // Read-only telemetry snapshot is open by design (carries no secrets,
            // mirrors the open status/sensors/logs GETs). State-changing WS
            // messages are gated separately in handleWebSocketMessage (#75).
            ApiServer::instance().sendInitialState(client);
            return;
        }
        if (type == WS_EVT_DISCONNECT) {
            ApiServer::instance().forgetWsClient(client);
            return;
        }
        if (type != WS_EVT_DATA) return;

        auto* info = static_cast<AwsFrameInfo*>(arg);
        if (!info || info->opcode != WS_TEXT || !info->final || info->index != 0) return;

        // Drop oversized frames instead of buffering them into the heap.
        if (len > MAX_POST_PAYLOAD) {
            if (client) client->close();
            return;
        }

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

void ApiServer::sendMetrics(AsyncWebServerRequest* request) const {
    auto& health = HealthMonitor::instance();
    const bool wifiUp = WiFiManager::instance().isConnected();

    // All series carry a device="<id>" label so a fleet scrape stays distinct.
    String dev = WiFiManager::instance().deviceId();
    dev.replace("\\", "");  // keep the label value clean
    dev.replace("\"", "");
    const String L = "{device=\"" + dev + "\"} ";

    String m;
    m.reserve(1024);
    auto metric = [&](const char* name, const char* type, const char* help, const String& value) {
        m += "# HELP "; m += name; m += " "; m += help; m += "\n";
        m += "# TYPE "; m += name; m += " "; m += type; m += "\n";
        m += name; m += L; m += value; m += "\n";
    };

    metric("openframe_uptime_seconds", "counter", "Device uptime in seconds.", String(millis() / 1000UL));
    metric("openframe_free_heap_bytes", "gauge", "Current free heap.", String(health.getFreeHeap()));
    metric("openframe_min_free_heap_bytes", "gauge", "Lowest free heap seen this session.", String(health.getMinFreeHeap()));
    metric("openframe_largest_free_block_bytes", "gauge", "Largest contiguous allocatable block.", String(health.getLargestFreeBlock()));
    metric("openframe_heap_fragmentation_percent", "gauge", "Heap fragmentation (0-100).", String(health.getHeapFragPercent()));
    metric("openframe_cpu_load_percent", "gauge", "Estimated CPU load (0-100).", String(static_cast<int>(health.getCpuLoadPercent())));
    metric("openframe_boot_count", "gauge", "Consecutive unstable boots.", String(health.getBootCount()));
    metric("openframe_safe_mode", "gauge", "1 if running in safe mode.", String(health.inSafeMode() ? 1 : 0));
    metric("openframe_wifi_up", "gauge", "1 if WiFi (STA) is connected.", String(wifiUp ? 1 : 0));
    metric("openframe_wifi_rssi_dbm", "gauge", "WiFi RSSI in dBm (0 if disconnected).", String(wifiUp ? WiFi.RSSI() : 0));
    metric("openframe_mqtt_up", "gauge", "1 if connected to the MQTT broker.", String(MqttManager::instance().isConnected() ? 1 : 0));

    request->send(200, "text/plain; version=0.0.4", m);
}

void ApiServer::sendSelfTest(AsyncWebServerRequest* request) const {
    JsonDocument doc;

    // RAM
    const uint32_t freeHeap = ESP.getFreeHeap();
    const bool ramOk = freeHeap > 20000;
    auto ram = doc["ram"].to<JsonObject>();
    ram["free_heap"]     = freeHeap;
    ram["min_free_heap"] = of_min_free_heap();
    ram["largest_block"] = of_largest_free_block();
    ram["ok"]            = ramOk;

    // Flash / sketch space
    const uint32_t freeSketch = ESP.getFreeSketchSpace();
    const bool flashOk = freeSketch > 0;
    auto flash = doc["flash"].to<JsonObject>();
    flash["sketch_size"] = ESP.getSketchSize();
    flash["free_space"]  = freeSketch;
    flash["ok"]          = flashOk;

    // Filesystem
    StorageInfo si = StorageManager::instance().info();
    const bool fsOk = si.total > 0;
    auto fs = doc["fs"].to<JsonObject>();
    fs["total"] = si.total;
    fs["used"]  = si.used;
    fs["free"]  = si.free;
    fs["ok"]    = fsOk;

    // I2C bus scan. On boards where the OLED isn't on the default I2C pins (e.g.
    // the C3's 0.42" panel on GPIO5/6), scanning the default bus finds nothing and
    // looks like a wiring fault even when the panel is fine. Scan on the first
    // configured display's pins when it specifies them, so this is a meaningful
    // "is the panel actually on the bus?" check. Falls back to the board default.
    int8_t scanSda = -1, scanScl = -1;
    for (const auto& cfg : DisplayManager::instance().displayConfigsCopy()) {
        if (cfg.sdaPin >= 0 && cfg.sclPin >= 0) { scanSda = cfg.sdaPin; scanScl = cfg.sclPin; break; }
    }
    auto i2c = doc["i2c"].to<JsonObject>();
    if (scanSda >= 0 && scanScl >= 0) {
        of_i2c_begin(scanSda, scanScl);  // rebind to the panel's pins if needed
        i2c["sda"] = scanSda;
        i2c["scl"] = scanScl;
    } else {
        of_i2c_begin();
    }
    auto found = i2c["devices"].to<JsonArray>();
    for (uint8_t addr = 1; addr < 127; ++addr) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) {
            char buf[6];
            snprintf(buf, sizeof(buf), "0x%02X", addr);
            found.add(buf);
        }
    }
    i2c["ok"] = true;  // an empty bus is valid; the scan completing is the pass

    // microSD: report mount state on boards with a slot we drive (S3-BOX). "No
    // card" is a valid, passing state — this surfaces whether one is present.
    of_sd_fill_json(doc["sd"].to<JsonObject>());

    doc["ok"] = ramOk && flashOk && fsOk;
    sendJson(request, doc);
}

void ApiServer::sendNetDiag(AsyncWebServerRequest* request) const {
    const auto& cfg = ConfigManager::instance().config();
    const bool up = WiFiManager::instance().isConnected();
    JsonDocument doc;

    auto wifi = doc["wifi"].to<JsonObject>();
    wifi["connected"] = up;
    if (up) {
        wifi["ssid"]    = WiFi.SSID();
        wifi["rssi"]    = WiFi.RSSI();
        wifi["ip"]      = WiFi.localIP().toString();
        wifi["gateway"] = WiFi.gatewayIP().toString();
        wifi["subnet"]  = WiFi.subnetMask().toString();
        wifi["dns"]     = WiFi.dnsIP().toString();
    }

    // DNS resolution — also a basic "is the internet reachable" probe.
    auto dns = doc["dns"].to<JsonObject>();
    const String host = cfg.time.ntpServer.length() ? cfg.time.ntpServer : String("pool.ntp.org");
    dns["host"] = host;
    if (up) {
        IPAddress addr;
        const uint32_t t0 = millis();
        const bool ok = WiFi.hostByName(host.c_str(), addr) == 1;
        dns["resolved"] = ok;
        dns["ms"]       = millis() - t0;
        if (ok) dns["ip"] = addr.toString();
    } else {
        dns["resolved"] = false;
    }

    // Broker reachability + TCP-connect latency (if MQTT is configured).
    auto mqtt = doc["mqtt"].to<JsonObject>();
    mqtt["enabled"]   = cfg.mqtt.enabled;
    mqtt["connected"] = MqttManager::instance().isConnected();
    if (cfg.mqtt.enabled && cfg.mqtt.host.length()) {
        mqtt["host"] = cfg.mqtt.host;
        mqtt["port"] = cfg.mqtt.port;
        if (up) {
            WiFiClient client;
            const uint32_t t0 = millis();
            const bool ok = client.connect(cfg.mqtt.host.c_str(), cfg.mqtt.port);
            mqtt["reachable"] = ok;
            mqtt["ms"]        = millis() - t0;
            client.stop();
        }
    }

    auto ntp = doc["ntp"].to<JsonObject>();
    ntp["source"] = TimeManager::instance().source();
    ntp["valid"]  = TimeManager::instance().isValid();
    ntp["epoch"]  = TimeManager::instance().epoch();

    sendJson(request, doc);
}

void ApiServer::sendConfig(AsyncWebServerRequest* request) const {
    JsonDocument doc;
    ConfigManager::instance().toJson(doc);
    // Read-only runtime identity, surfaced alongside config for the Settings UI.
    doc["device"]["id"] = WiFiManager::instance().deviceId();

    // ETag so a polling client can revalidate cheaply (#33). FNV-1a over the body.
    const String body = toJsonString(doc);
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < body.length(); ++i) { h ^= static_cast<uint8_t>(body[i]); h *= 16777619u; }
    const String etag = "\"" + String(h, 16) + "\"";

    if (request->hasHeader("If-None-Match") && request->header("If-None-Match") == etag) {
        AsyncWebServerResponse* res = request->beginResponse(304);
        res->addHeader("ETag", etag);
        request->send(res);
        return;
    }
    AsyncWebServerResponse* res = request->beginResponse(200, "application/json", body);
    res->addHeader("ETag", etag);
    res->addHeader("Cache-Control", "no-cache");  // cache, but always revalidate
    request->send(res);
}

void ApiServer::sendWifiScan(AsyncWebServerRequest* request) const {
    JsonDocument doc;
    WiFiManager::instance().scanNearbyNetworks(doc);
    sendJson(request, doc);
}

void ApiServer::sendVariables(AsyncWebServerRequest* request) const {
    // Build the doc directly and serialize once. The old path went
    // doc→String→doc→String (buildVariablesJson serialized, then we parsed it
    // back, then sendJson serialized again) — three large heap blocks live at
    // once, which is exactly what fails on a fragmented classic-ESP32 heap and
    // closes the socket with no bytes sent (browser: ERR_EMPTY_RESPONSE).
    JsonDocument doc;
    buildVariablesDoc(doc);
    sendJson(request, doc);
}

void ApiServer::sendLogs(AsyncWebServerRequest* request) const {
    JsonDocument doc;
    buildLogsDoc(doc);
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

    // Snapshot the current (pre-change) config first, so a bad push can be rolled
    // back in one tap from the backups list. `before` is the same snapshot used to
    // decide whether the change can apply live.
    JsonDocument before;
    ConfigManager::instance().toJson(before);
    createConfigBackup();

    {
        // Hold the config lock across the write: fromJson reassigns the config
        // Strings on this (web) task while loop-task managers may be copying
        // them (see ConfigManager::mutex()).
        of_lock_guard<of_recursive_mutex> lock(ConfigManager::instance().mutex());
        ConfigManager::instance().fromJson(doc);
    }
    // The parsed request is applied — free it before allocating the post-save
    // snapshot. This handler runs on the async task, and every concurrent
    // config-sized allocation counts on the ESP8266's heap.
    doc.clear();
    if (!ConfigManager::instance().save()) {
        sendError(request, 500, "Failed to save config");
        return;
    }

    JsonDocument after;
    ConfigManager::instance().toJson(after);

    // Hot-apply decision + manager dispatch live in ConfigHotApply, shared with
    // the MQTT apply_config fleet command so the two write paths can't drift.
    const bool hotApplicable = applyConfigHot(before, after);
    // The diff is done and `after` doubles as the response body (it already is
    // the fresh full config) — release the pre-change snapshot.
    before.clear();

    JsonDocument& response = after;
    if (hotApplicable) {
        response["restartRequired"] = false;
        response["message"] = "Settings saved and applied.";
    } else {
        response["restartRequired"] = true;
        response["message"] = "Settings saved. Restarting to apply changes.";
    }
    sendJson(request, response);
    broadcastFrame("config_change", response);
    publishHealthUpdate();

    if (!hotApplicable) scheduleRestart();
}

// ── Config backup/restore slots ─────────────────────────────────────────────
// Slots live in OF_CONFIG_BACKUP_DIR as "<id>.json" (id = epoch when the clock is
// known, else millis()), each a verbatim copy of /config.json.

String ApiServer::createConfigBackup() const {
    auto& storage = StorageManager::instance();
    String body;
    if (!storage.readRaw(OF_CONFIG_PATH, body) || body.isEmpty()) return "";

    storage.mkdirs(OF_CONFIG_BACKUP_DIR);
    const uint32_t epoch = TimeManager::instance().epoch();
    const uint32_t id = (epoch > 1700000000UL) ? epoch : millis();
    const String path = String(OF_CONFIG_BACKUP_DIR) + "/" + String(id) + ".json";
    if (!storage.writeRaw(path, body)) return "";

    pruneConfigBackups(OF_CONFIG_BACKUP_KEEP);
    LOG_I(TAG, "Config backup created: " + path);
    return path;
}

void ApiServer::pruneConfigBackups(size_t keep) const {
    auto entries = StorageManager::instance().listEntries(OF_CONFIG_BACKUP_DIR);
    std::vector<String> names;
    for (const auto& e : entries) {
        if (!e.isDir && e.name.endsWith(".json")) names.push_back(e.name);
    }
    if (names.size() <= keep) return;
    // Oldest first: ids are numeric (epoch/millis), so numeric sort = chronological.
    std::sort(names.begin(), names.end(), [](const String& a, const String& b) {
        return strtoul(a.c_str(), nullptr, 10) < strtoul(b.c_str(), nullptr, 10);
    });
    for (size_t i = 0; i + keep < names.size(); ++i) {
        StorageManager::instance().remove(String(OF_CONFIG_BACKUP_DIR) + "/" + names[i]);
    }
}

void ApiServer::sendConfigBackups(AsyncWebServerRequest* request) const {
    JsonDocument doc;
    auto arr = doc["backups"].to<JsonArray>();
    auto entries = StorageManager::instance().listEntries(OF_CONFIG_BACKUP_DIR);
    for (const auto& e : entries) {
        if (e.isDir || !e.name.endsWith(".json")) continue;
        String id = e.name;
        id.remove(id.length() - 5);  // strip ".json"
        auto o = arr.add<JsonObject>();
        o["id"] = id;
        o["size"] = e.size;
        // id is an epoch when > the sentinel, else a relative millis stamp.
        const uint32_t n = strtoul(id.c_str(), nullptr, 10);
        o["epoch"] = (n > 1700000000UL) ? n : 0;
    }
    doc["count"] = arr.size();
    sendJson(request, doc);
}

void ApiServer::handleConfigBackupCreate(AsyncWebServerRequest* request) {
    const String path = createConfigBackup();
    if (path.isEmpty()) {
        sendError(request, 500, "Failed to create backup");
        return;
    }
    sendConfigBackups(request);
}

void ApiServer::handleConfigRestore(AsyncWebServerRequest* request, const String& body) {
    JsonDocument req;
    if (deserializeJson(req, body)) {
        sendError(request, 400, "Invalid JSON");
        return;
    }
    const String id = req["id"] | String("");
    if (!id.length() || id.indexOf('/') >= 0 || id.indexOf("..") >= 0) {
        sendError(request, 400, "Valid backup id required");
        return;
    }
    const String path = String(OF_CONFIG_BACKUP_DIR) + "/" + id + ".json";
    String snapshot;
    if (!StorageManager::instance().readRaw(path, snapshot) || snapshot.isEmpty()) {
        sendError(request, 404, "Backup not found");
        return;
    }
    // Validate it parses before clobbering the live config.
    JsonDocument check;
    if (deserializeJson(check, snapshot)) {
        sendError(request, 422, "Backup is not valid JSON");
        return;
    }
    // Snapshot the current config too, so a restore is itself undoable.
    createConfigBackup();
    if (!StorageManager::instance().writeRaw(OF_CONFIG_PATH, snapshot)) {
        sendError(request, 500, "Failed to write restored config");
        return;
    }
    // writeRaw doesn't touch the NVS config mirror; sync it so a boot with an
    // unreadable LittleFS doesn't fall back to the pre-restore config.
    ConfigManager::instance().syncNvsBackup(check);

    JsonDocument response;
    response["ok"] = true;
    response["restored"] = id;
    response["restartRequired"] = true;
    response["message"] = "Config restored. Restarting to apply.";
    sendJson(request, response);
    scheduleRestart();
}

void ApiServer::handleConfigBackupDelete(AsyncWebServerRequest* request) {
    const String url = request->url();
    const String id = url.substring(url.lastIndexOf('/') + 1);
    if (!id.length() || id.indexOf("..") >= 0) {
        sendError(request, 400, "Valid backup id required");
        return;
    }
    const bool removed = StorageManager::instance().remove(String(OF_CONFIG_BACKUP_DIR) + "/" + id + ".json");
    JsonDocument doc;
    doc["ok"] = removed;
    doc["id"] = id;
    sendJson(request, doc, removed ? 200 : 404);
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
    buildVariablesDoc(response);
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

    // First-message auth handshake (#75): a browser can't set an Authorization
    // header on the WS upgrade, so a client proves it holds the api_token by
    // sending {"type":"auth","payload":{"token":"…"}} once after connecting.
    if (type == "auth") {
        const String& token = ConfigManager::instance().config().device.apiToken;
        const String provided = payload["token"] | String("");
        const bool ok = token.isEmpty() || tokenEquals(provided, token);
        if (ok && client) _authedWsClients.insert(client->id());
        response["ok"] = ok;
        if (!ok) response["error"] = "unauthorized";
        sendFrame(client, "auth_result", response);
        return;
    }

    // State-changing WS messages require auth when a token is configured. Without
    // this, the token-gated REST writes (#75) could be bypassed over the socket
    // (e.g. config_save persists config). Read-only frames (ping, snapshots) and
    // the open-LAN default (no token) are unaffected.
    const bool mutating = (type == "page_navigation" || type == "config_save" ||
                           type == "config_change" || type == "action_trigger");
    if (mutating && !wsClientAuthed(client)) {
        response["ok"] = false;
        response["error"] = "unauthorized";
        sendFrame(client, "command_result", response);
        return;
    }

    if (type == "page_navigation") {
        const String displayId = payload["displayId"] | String("");
        // action: "next"/"prev" use the device-authoritative page order (F6); an
        // explicit pageId still does a direct goto. Keeps ordering on the device so
        // the dashboard never re-derives it.
        const String action = payload["action"] | String("");
        bool ok;
        if (action == "next")      ok = DisplayManager::instance().nextPage(displayId);
        else if (action == "prev") ok = DisplayManager::instance().previousPage(displayId);
        else ok = DisplayManager::instance().setActivePage(displayId, payload["pageId"] | String(""));
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

bool ApiServer::wsClientAuthed(AsyncWebSocketClient* client) const {
    // No token configured → LAN-trusted, everything allowed.
    if (ConfigManager::instance().config().device.apiToken.isEmpty()) return true;
    if (!client) return false;
    return _authedWsClients.count(client->id()) > 0;
}

void ApiServer::forgetWsClient(AsyncWebSocketClient* client) {
    if (client) _authedWsClients.erase(client->id());
}

void ApiServer::sendInitialState(AsyncWebSocketClient* client) const {
    if (!client) return;
    // Under heap pressure, skip the (larger) initial snapshot rather than risk the
    // throw-on-fail allocation aborting the device; the live stream still resumes
    // once memory recovers.
    if (!wsSendSafe()) return;

    JsonDocument statusDoc;
    deserializeJson(statusDoc, buildStatusJson());
    sendFrame(client, "health", statusDoc);

    const String variablesJson = buildVariablesJson();
    JsonDocument variablesDoc;
    deserializeJson(variablesDoc, variablesJson);
    String variablePayload;
    serializeJson(variablesDoc["variables"], variablePayload);
    sendRawFrame(client, "variable_snapshot", variablePayload);

    // Bounded, copy-free log snapshot: serialise only the most recent entries
    // straight into one array. Avoids getEntries()' full deep copy and the
    // build-string → re-parse → re-serialise round trip that OOM'd the tight heap
    // on every WebSocket connect. The live stream delivers newer lines after this.
    JsonDocument logsDoc;
    auto logEntries = logsDoc.to<JsonArray>();
    Logger::instance().forEachEntry([&](const LogEntry& entry) {
        serializeLogEntry(logEntries.add<JsonObject>(), entry);
    }, OF_WS_LOG_SNAPSHOT_MAX);
    String logPayload;
    serializeJson(logsDoc, logPayload);
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
    // Skip the per-line JSON build when nobody is listening, and on constrained
    // boards only stream Warning+ (the full ring is still readable via REST).
    if (static_cast<uint8_t>(entry.level) < OF_LOG_WS_MIN_LEVEL) return;
    if (_ws.count() == 0) return;

    JsonDocument doc;
    auto obj = doc.to<JsonObject>();
    serializeLogEntry(obj, entry);
    broadcastFrame("log", doc);
}

void ApiServer::sendJson(AsyncWebServerRequest* request, const JsonDocument& doc, int status) const {
    if (!request) return;
    request->send(status, "application/json", toJsonString(doc));
}

// Constant-time string compare. Length differs → short-circuit (length isn't
// the secret); equal lengths are compared without an early-out so the time taken
// doesn't leak how many leading characters matched.
static bool tokenEquals(const String& a, const String& b) {
    if (a.length() != b.length()) return false;
    uint8_t diff = 0;
    for (size_t i = 0; i < a.length(); ++i) {
        diff |= static_cast<uint8_t>(a[i]) ^ static_cast<uint8_t>(b[i]);
    }
    return diff == 0;
}

bool ApiServer::requireAuth(AsyncWebServerRequest* request) const {
    // CSRF defence (#77): reject a cross-origin browser request before any auth/
    // side effect. A missing Origin (curl, Home Assistant, same-origin navigations)
    // is allowed — CSRF is a browser-only attack. Enforced even when no token is
    // set, so the open-LAN default isn't drivable from a malicious web page.
    if (request->hasHeader("Origin")) {
        const String origin = request->header("Origin");
        const int schemeEnd = origin.indexOf("://");
        const String originHost = schemeEnd >= 0 ? origin.substring(schemeEnd + 3) : origin;
        if (originHost != request->host()) {
            request->send(403, "application/json", "{\"error\":\"cross-origin request blocked\"}");
            return false;
        }
    }

    const String& token = ConfigManager::instance().config().device.apiToken;
    if (token.isEmpty()) return true;  // LAN-trusted default — auth disabled

    // Header-only: never accept the token via query string — query strings leak
    // through server logs, browser history, and the Referer header. Callers that
    // can't set headers (download links) fetch the bytes and build a blob URL.
    String provided;
    if (request->hasHeader("Authorization")) {
        const String h = request->header("Authorization");
        if (h.startsWith("Bearer ")) provided = h.substring(7);
    }
    if (tokenEquals(provided, token)) return true;

    request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
    return false;
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

// Assemble a {"type":...,"payload":...} frame into a single pre-reserved String.
// Chained operator+ would allocate a fresh heap String at every "+" (4+ per frame),
// fragmenting the heap on memory-tight boards; one reserve()d buffer avoids that.
static String buildWsFrame(const char* type, const String& payloadJson) {
    const char* payload = payloadJson.length() ? payloadJson.c_str() : "null";
    const size_t payloadLen = payloadJson.length() ? payloadJson.length() : 4;
    String frame;
    frame.reserve(20 + strlen(type) + payloadLen);
    frame += "{\"type\":\"";
    frame += type;
    frame += "\",\"payload\":";
    frame += payload;
    frame += "}";
    return frame;
}

// ESPAsyncWebServer's text()/textAll() copy the frame into a std::vector whose
// allocation THROWS on failure — an uncaught bad_alloc that reboots the device
// (decoded from a field crash: WS connect during a post-reboot asset re-stream).
// The wsCanSend() gates make that allocation all but certain to succeed; this
// wrapper closes the remaining check-to-alloc race (another task allocating in
// between). Dropping the frame is always better than aborting the firmware.
#if defined(__EXCEPTIONS)
template <typename SendFn>
static void wsGuardedSend(SendFn&& send) {
    try {
        send();
    } catch (...) {
        // Frame dropped — heap raced below the gate. Live updates resume once
        // memory recovers; deliberately no logging here (logging allocates).
    }
}
#else
template <typename SendFn>
static void wsGuardedSend(SendFn&& send) {
    send();
}
#endif

void ApiServer::sendFrame(AsyncWebSocketClient* client, const char* type, const JsonDocument& payload) const {
    if (!client) return;
    sendRawFrame(client, type, toJsonString(payload));
}

void ApiServer::sendRawFrame(AsyncWebSocketClient* client, const char* type, const String& rawPayloadJson) const {
    if (!client) return;
    const String frame = buildWsFrame(type, rawPayloadJson);
    if (!wsCanSend(frame.length())) return;
    wsGuardedSend([&] { client->text(frame); });
}

bool ApiServer::wsSendSafe() const {
    return wsCanSend(0);
}

bool ApiServer::wsCanSend(size_t frameLen) const {
#if defined(ESP8266)
    const uint32_t largest = ESP.getMaxFreeBlockSize();
#else
    const uint32_t largest = ESP.getMaxAllocHeap();
#endif
    // The library copies the frame into one contiguous buffer, so the largest
    // free block must fit the frame itself plus headroom for the send queue.
    const uint32_t needBlock = std::max<uint32_t>(WS_MIN_ALLOC_BLOCK, frameLen + WS_SEND_HEADROOM);
    return ESP.getFreeHeap() >= WS_MIN_FREE_HEAP + frameLen && largest >= needBlock;
}

void ApiServer::broadcastFrame(const char* type, const JsonDocument& payload) {
    if (_ws.count() == 0) return;   // no subscribers — skip serialize + send entirely
    if (!wsSendSafe()) return;      // heap too low/fragmented to even serialize
    broadcastRawFrame(type, toJsonString(payload));
}

void ApiServer::broadcastRawFrame(const char* type, const String& rawPayloadJson) {
    if (_ws.count() == 0) return;
    if (!wsSendSafe()) return;
    const String frame = buildWsFrame(type, rawPayloadJson);
    if (!wsCanSend(frame.length())) return;
    wsGuardedSend([&] { _ws.textAll(frame); });
}

String ApiServer::buildStatusJson() const {
    JsonDocument doc;
    const auto& config = ConfigManager::instance().config();
    const uint32_t uptimeMs = millis();
    const bool wifiConnected = WiFiManager::instance().isConnected();

    doc["deviceId"] = WiFiManager::instance().deviceId();
    doc["name"] = config.device.name;
    doc["board"] = config.device.boardType;
    doc["version"] = OF_VERSION_STRING;
    doc["uptime_ms"] = uptimeMs;
    doc["uptime"] = formatUptime(uptimeMs);
    doc["freeHeap"] = ESP.getFreeHeap();
    doc["minFreeHeap"] = of_min_free_heap();
    doc["largestFreeBlock"] = HealthMonitor::instance().getLargestFreeBlock();
    doc["heapFragPercent"] = HealthMonitor::instance().getHeapFragPercent();
    doc["freePsram"] = of_free_psram();
    doc["psramSize"] = of_psram_size();
    doc["cpuLoadPercent"] = static_cast<int>(HealthMonitor::instance().getCpuLoadPercent());
    doc["rebootReason"] = HealthMonitor::instance().getRebootReason();
    doc["safeMode"] = HealthMonitor::instance().inSafeMode();
    doc["wifiConnected"] = wifiConnected;
    doc["apMode"] = WiFiManager::instance().isApMode();
    doc["ip"] = WiFiManager::instance().localIP();
    doc["hostname"] = WiFiManager::instance().hostname();
    doc["mdnsHost"] = WiFiManager::instance().hostname() + ".local";
    doc["rssi"] = wifiConnected ? WiFi.RSSI() : 0;
    doc["mqttEnabled"] = config.mqtt.enabled;
    if (config.mqtt.enabled) {
        doc["mqttConnected"] = MqttManager::instance().isConnected();
        const String mqttErr = MqttManager::instance().lastError();
        if (mqttErr.length()) doc["mqttLastError"] = mqttErr;
    }
    doc["haEnabled"] = config.ha.enabled;
    doc["otaEnabled"] = config.ota.enabled;

    // Compile-time capability flags — the frontend gates feature cards/routes on
    // these instead of guessing from the board name. Key names are a contract.
    auto features = doc["features"].to<JsonObject>();
    features["weather"]      = (OF_ENABLE_WEATHER != 0);
    features["push"]         = (OF_ENABLE_PUSH != 0);
    features["ha_ws"]        = (OF_ENABLE_HA_WS != 0);
    features["images"]       = (OF_ENABLE_IMAGES != 0);
    features["tft"]          = (OF_ENABLE_TFT != 0);
    features["sd"]           = (OF_ENABLE_SD != 0);
    features["hw_variables"] = (OF_ENABLE_HW_VARIABLES != 0);
    features["tls"]          = (OF_ENABLE_TLS != 0);

    doc["logCount"] = Logger::instance().getEntryCount();
    doc["variableCount"] = VariableManager::instance().all().size();
    doc["moduleCount"] = ModuleManager::instance().modules().size();
    doc["i2cErrorCount"] = ModuleManager::instance().i2cErrorCount();

    // Sensor failure flags (locked snapshot — sensors hot-reload on the loop task).
    auto sensorArr = doc["sensors"].to<JsonArray>();
    uint32_t sensorErrorTotal = 0;
    SensorManager::instance().fillHealthJson(sensorArr, sensorErrorTotal);
    doc["sensorErrorCount"] = sensorErrorTotal;

    doc["activeProfileId"] = ProfileManager::instance().activeId();
    doc["time"] = TimeManager::instance().epoch();
    doc["timeSource"] = TimeManager::instance().source();

    // NodeLink (ESP-NOW mesh) live status: which peers we've recently heard. A
    // peer is "online" if seen within PEER_TIMEOUT (2× the 10s announce interval).
    {
        auto& nl = NodeLinkManager::instance();
        auto nodelink = doc["nodelink"].to<JsonObject>();
        nodelink["enabled"] = nl.enabled();
        const uint32_t now = millis();
        constexpr uint32_t PEER_TIMEOUT_MS = 25000;
        size_t online = 0;
        auto peers = nodelink["peers"].to<JsonArray>();
        for (const auto& kv : nl.peers()) {
            const uint32_t ageMs = now - kv.second.lastSeenMs;
            const bool isOnline = ageMs < PEER_TIMEOUT_MS;
            if (isOnline) ++online;
            auto p = peers.add<JsonObject>();
            p["id"]     = kv.second.id;
            p["name"]   = kv.second.name;
            p["ageMs"]  = ageMs;
            p["online"] = isOnline;
        }
        nodelink["peerCount"]  = online;
        nodelink["totalSeen"]  = nl.peers().size();
    }

    return toJsonString(doc);
}

void ApiServer::buildVariablesDoc(JsonDocument& doc) const {
    auto variables = doc["variables"].to<JsonObject>();
    for (const Variable* var : VariableManager::instance().all()) {
        if (!var) continue;
        auto obj = variables[var->id].to<JsonObject>();
        serializeVariable(obj, *var);
    }
}

String ApiServer::buildVariablesJson() const {
    JsonDocument doc;
    buildVariablesDoc(doc);
    return toJsonString(doc);
}

void ApiServer::buildLogsDoc(JsonDocument& doc, size_t maxCount) const {
    auto entries = doc["entries"].to<JsonArray>();
    Logger::instance().forEachEntry([&](const LogEntry& entry) {
        serializeLogEntry(entries.add<JsonObject>(), entry);
    }, maxCount);
    doc["count"] = Logger::instance().getEntryCount();
}

String ApiServer::buildLogsJson(size_t maxCount) const {
    JsonDocument doc;
    buildLogsDoc(doc, maxCount);
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

    // Read-only mirrors (sensor metrics, input states) reflect live hardware and
    // cannot be set from the API. Writable output mirrors fall through and drive
    // the hardware via the VariableManager change subscription (F1).
    if (var->readOnly) {
        error = "Variable is read-only: " + id;
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

void ApiServer::sendOutputsState(AsyncWebServerRequest* request) const {
    JsonDocument doc;
    auto arr = doc["outputs"].to<JsonArray>();
    OutputManager::instance().fillStateJson(arr);
    doc["count"] = arr.size();
    sendJson(request, doc);
}

void ApiServer::handleOutputsControl(AsyncWebServerRequest* request, const String& body) {
    JsonDocument req;
    if (deserializeJson(req, body)) {
        sendError(request, 400, "Invalid JSON");
        return;
    }
    const String id      = req["id"]      | String("");
    const String command = req["command"] | String("");
    if (!id.length() || !command.length()) {
        sendError(request, 400, "id and command are required");
        return;
    }

    auto& outputs = OutputManager::instance();
    auto clampByte = [](int v) -> uint8_t { return static_cast<uint8_t>(v < 0 ? 0 : (v > 255 ? 255 : v)); };

    bool ok = false;
    if (command == "digital") {
        ok = outputs.setDigital(id, req["on"] | false);
    } else if (command == "brightness") {
        ok = outputs.setBrightness(id, clampByte(req["brightness"] | 0));
    } else if (command == "rgb") {
        ok = outputs.setRgb(id, clampByte(req["r"] | 0), clampByte(req["g"] | 0), clampByte(req["b"] | 0));
    } else if (command == "animation") {
        // Optional colour rides along with the animation request.
        if (req["r"].is<int>() || req["g"].is<int>() || req["b"].is<int>()) {
            outputs.setRgb(id, clampByte(req["r"] | 0), clampByte(req["g"] | 0), clampByte(req["b"] | 0));
        }
        const LedAnimation anim = ledAnimationFromString(req["animation"] | String("solid"));
        ok = outputs.setAnimation(id, anim, clampByte(req["speed"] | 0));
    } else if (command == "beep") {
        ok = outputs.beep(id, static_cast<uint16_t>(req["frequency"] | 1000),
                              static_cast<uint16_t>(req["duration_ms"] | 200));
    } else if (command == "angle") {
        const int angle = req["angle"] | 90;
        ok = outputs.setAngle(id, static_cast<uint8_t>(angle < 0 ? 0 : (angle > 180 ? 180 : angle)));
    } else if (command == "move") {
        ok = outputs.setPosition(id, req["position"] | 0);  // absolute step target
    } else {
        sendError(request, 400, "Unknown command");
        return;
    }

    if (!ok) {
        sendError(request, 404, "Unknown output, or command not supported for its type");
        return;
    }

    JsonDocument doc;
    doc["ok"] = true;
    auto arr = doc["outputs"].to<JsonArray>();
    outputs.fillStateJson(arr);
    sendJson(request, doc);
}

void ApiServer::sendSensors(AsyncWebServerRequest* request) const {
    JsonDocument doc;
    if (!StorageManager::instance().readJson(OF_SENSORS_PATH, doc)) {
        SensorManager::instance().fillConfigJson(doc["sensors"].to<JsonArray>());
    }
    doc["count"] = doc["sensors"].is<JsonArrayConst>() ? doc["sensors"].as<JsonArrayConst>().size() : 0;
    sendJson(request, doc);
}

void ApiServer::sendHaImport(AsyncWebServerRequest* request) const {
    JsonDocument doc;
    if (!StorageManager::instance().readJson(OF_HA_IMPORT_PATH, doc)) {
        doc["entities"].to<JsonArray>();
    }
    doc["count"] = doc["entities"].is<JsonArrayConst>() ? doc["entities"].as<JsonArrayConst>().size() : 0;
    sendJson(request, doc);
}

void ApiServer::sendScreens(AsyncWebServerRequest* request) const {
    JsonDocument doc;
    DisplayManager::instance().fillScreensJson(doc["screens"].to<JsonArray>());
    doc["count"] = doc["screens"].as<JsonArrayConst>().size();
    sendJson(request, doc);
}

#if defined(OF_ENABLE_XPT2046_TOUCH)
void ApiServer::sendTouchCalibration(AsyncWebServerRequest* request) const {
    JsonDocument doc;
    TouchManager::instance().fillTouchCalibrationJson(doc.to<JsonObject>());
    sendJson(request, doc);
}

void ApiServer::handleTouchUpdate(AsyncWebServerRequest* request, const String& body) {
    JsonDocument doc;
    if (deserializeJson(doc, body)) {
        sendError(request, 400, "Invalid JSON");
        return;
    }
    String error;
    if (!TouchManager::instance().applyTouchCalibration(doc.as<JsonVariantConst>(), error)) {
        sendError(request, 400, error);
        return;
    }
    JsonDocument response;
    response["ok"] = true;
    response["message"] = "Touch calibration saved and applied.";
    sendJson(request, response);
}
#endif

void ApiServer::sendDisplays(AsyncWebServerRequest* request) const {
    JsonDocument doc;

    // A board with an integrated panel (S3-GEEK/-BOX) seeds a built-in display in
    // memory when no config is persisted — but displays.json still exists as an
    // empty array, so a plain read returns nothing and the UI shows no display.
    // Fall back to the live DisplayManager configs whenever the persisted list is
    // missing OR empty, emitting the full field set so the seeded default is
    // visible and editable (and saving it persists it verbatim).
    const bool read = StorageManager::instance().readJson(OF_DISPLAYS_PATH, doc);
    const bool hasPersisted = read && doc["displays"].is<JsonArrayConst>()
                              && doc["displays"].as<JsonArrayConst>().size() > 0;

    if (!hasPersisted) {
        doc.clear();
        auto arr = doc["displays"].to<JsonArray>();
        for (const auto& cfg : DisplayManager::instance().displayConfigsCopy()) {
            auto obj = arr.add<JsonObject>();
            obj["id"]      = cfg.id;
            obj["type"]    = cfg.type;
            obj["enabled"] = cfg.enabled;
            obj["width"]   = cfg.width;
            obj["height"]  = cfg.height;
            obj["address"] = cfg.address;
            obj["rotation"]            = cfg.rotation;
            obj["refresh_interval_ms"] = cfg.refreshIntervalMs;
            obj["contrast"]            = cfg.contrast;
            obj["spi_frequency"]       = cfg.spiFrequency;
            // Brightness + night mode — emitted with their defaults so the seeded
            // display is fully editable and a save round-trips every field.
            obj["brightness"]       = cfg.brightness;
            obj["night_enabled"]    = cfg.nightEnabled;
            obj["night_start_min"]  = cfg.nightStartMin;
            obj["night_end_min"]    = cfg.nightEndMin;
            obj["night_brightness"] = cfg.nightBrightness;
            obj["night_blank"]      = cfg.nightBlank;
            if (cfg.initialPageId.length()) obj["page"] = cfg.initialPageId;
            // Emit only the pins that are actually wired (>= 0) so the UI's pin
            // selectors show blank for unused fields rather than "-1".
            if (cfg.resetPin >= 0)     obj["reset_pin"] = cfg.resetPin;
            if (cfg.sdaPin >= 0)       obj["sda_pin"]   = cfg.sdaPin;
            if (cfg.sclPin >= 0)       obj["scl_pin"]   = cfg.sclPin;
            if (cfg.csPin >= 0)        obj["cs_pin"]    = cfg.csPin;
            if (cfg.dcPin >= 0)        obj["dc_pin"]    = cfg.dcPin;
            if (cfg.mosiPin >= 0)      obj["mosi_pin"]  = cfg.mosiPin;
            if (cfg.sckPin >= 0)       obj["sck_pin"]   = cfg.sckPin;
            if (cfg.backlightPin >= 0) obj["bl_pin"]    = cfg.backlightPin;
            if (cfg.backlightActiveLow) obj["bl_active_low"] = true;
            // Sub-window mapping (small OLEDs) — only when not auto.
            if (cfg.colOffset != 0xFF)  obj["col_offset"]  = cfg.colOffset;
            if (cfg.pageOffset != 0xFF) obj["page_offset"] = cfg.pageOffset;
            if (cfg.comPins >= 0)       obj["com_pins"]    = cfg.comPins;
        }
        // Flag so the UI can hint that this is an unsaved board default.
        doc["seeded"] = true;
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
        if (!pageDoc["bg"].isNull()) page["bg"] = pageDoc["bg"];

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
                if (!item["action"].isNull()) out["action"] = item["action"] | String("");
                if (!item["target"].isNull()) out["target"] = item["target"] | String("");
                if (!item["step"].isNull())   out["step"]   = item["step"].as<float>();
                // Rich-widget geometry/style (camelCase back to the designer).
                if (!item["w"].isNull())      out["w"]      = item["w"].as<int>();
                if (!item["h"].isNull())      out["h"]      = item["h"].as<int>();
                if (!item["align"].isNull())  out["align"]  = item["align"].as<int>();
                if (!item["color"].isNull())  out["color"]  = item["color"];
                if (!item["bg"].isNull())     out["bg"]     = item["bg"];
                if (!item["filled"].isNull()) out["filled"] = item["filled"].as<bool>();
                if (!item["min"].isNull())    out["min"]    = item["min"].as<float>();
                if (!item["max"].isNull())    out["max"]    = item["max"].as<float>();
            }
        }
    }

    doc["count"] = arr.size();
    sendJson(request, doc);
}

// Keep image file names to a safe basename: alnum + . _ - only, no path separators,
// length-capped. Empty result means "reject".
static String sanitizeImageName(const String& in) {
    String out;
    for (size_t i = 0; i < in.length() && out.length() < 48; ++i) {
        const char c = in[i];
        if (isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '_' || c == '-') out += c;
    }
    if (out == "." || out == "..") return String("");
    return out;
}

void ApiServer::sendImages(AsyncWebServerRequest* request) const {
    JsonDocument doc;
    auto arr = doc["images"].to<JsonArray>();
#if OF_ENABLE_IMAGES
    for (const auto& e : StorageManager::instance().listEntries(OF_IMAGES_PATH)) {
        if (e.isDir) continue;
        auto o = arr.add<JsonObject>();
        o["name"] = e.name;
        o["size"] = e.size;
    }
    doc["supported"] = true;
#else
    doc["supported"] = false;
#endif
    doc["count"] = arr.size();
    sendJson(request, doc);
}

void ApiServer::handleImageDelete(AsyncWebServerRequest* request) {
    if (!requireAuth(request)) return;
    const String name = sanitizeImageName(request->hasParam("name") ? request->getParam("name")->value() : String(""));
    if (!name.length()) { sendError(request, 400, "Missing/invalid name"); return; }
    const bool ok = StorageManager::instance().remove(String(OF_IMAGES_PATH) + "/" + name);
    DisplayManager::instance().evictImage(name);
    JsonDocument res;
    res["ok"] = ok;
    sendJson(request, res);
}

void ApiServer::handleImageUploadChunk(AsyncWebServerRequest* request, uint8_t* data,
                                       size_t len, size_t index, size_t total) {
#if OF_ENABLE_IMAGES
    if (total > OF_IMAGE_MAX_BYTES) {
        if (index + len >= total) sendError(request, 413, "Image too large");
        return;
    }
    if (index == 0) {
        if (!requireAuth(request)) return;
        const String name = sanitizeImageName(request->hasParam("name") ? request->getParam("name")->value() : String(""));
        if (!name.length()) { sendError(request, 400, "Missing/invalid name"); return; }
        StorageManager::instance().mkdirs(OF_IMAGES_PATH);
        File* f = new File(LittleFS.open(String(OF_IMAGES_PATH) + "/" + name, "w"));
        if (!f || !*f) { delete f; sendError(request, 500, "Could not open image for writing"); return; }
        request->_tempObject = f;
        // Close + free a partially-written file if the connection drops mid-upload;
        // loadImage() rejects truncated files by header/size validation.
        request->onDisconnect([request]() {
            File* tf = static_cast<File*>(request->_tempObject);
            if (tf) { tf->close(); delete tf; request->_tempObject = nullptr; }
        });
    }
    File* f = static_cast<File*>(request->_tempObject);
    if (!f) return;
    if (len) f->write(data, len);
    if (index + len == total) {
        const String name = sanitizeImageName(request->hasParam("name") ? request->getParam("name")->value() : String(""));
        f->close();
        delete f;
        request->_tempObject = nullptr;
        DisplayManager::instance().evictImage(name);  // show the new image without a reboot
        JsonDocument res;
        res["ok"] = true;
        res["name"] = name;
        sendJson(request, res);
    }
#else
    (void)data; (void)len;
    if (index + len >= total) sendError(request, 501, "Images not supported on this board");
#endif
}

void ApiServer::handleInputsUpdate(AsyncWebServerRequest* request, const String& body) {
    JsonDocument source;
    if (deserializeJson(source, body)) {
        sendError(request, 400, "Invalid JSON");
        return;
    }

    // Require at least one of the expected arrays so a malformed request can't
    // wipe the inputs config to an empty file.
    if (!source["digital"].is<JsonArrayConst>() && !source["analog"].is<JsonArrayConst>()) {
        sendError(request, 400, "Missing 'digital'/'analog' arrays");
        return;
    }

    JsonDocument toSave;
    auto digital = toSave["digital"].to<JsonArray>();
    auto analog = toSave["analog"].to<JsonArray>();
    auto encoders = toSave["encoders"].to<JsonArray>();
    auto keypads = toSave["keypads"].to<JsonArray>();

    if (source["digital"].is<JsonArrayConst>()) {
        for (JsonObjectConst item : source["digital"].as<JsonArrayConst>()) {
            auto out = digital.add<JsonObject>();
            out["id"] = item["id"] | String("");
            out["pin"] = item["pin"] | 0;
            out["pullup"] = item["pullup"] | true;
            out["inverted"] = item["inverted"] | false;
            if (item["pulldown"].is<bool>()) out["pulldown"] = item["pulldown"].as<bool>();
            if (item["touch"].is<bool>()) out["touch"] = item["touch"].as<bool>();
            if (item["touch_threshold"].is<int>()) out["touch_threshold"] = item["touch_threshold"].as<int>();
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

    if (source["encoders"].is<JsonArrayConst>()) {
        for (JsonObjectConst item : source["encoders"].as<JsonArrayConst>()) {
            auto out = encoders.add<JsonObject>();
            out["id"]         = item["id"] | String("");
            out["pin_a"]      = item["pin_a"] | 0;
            out["pin_b"]      = item["pin_b"] | 0;
            out["pin_button"] = item["pin_button"] | 0;
            out["pullup"]     = item["pullup"] | true;
            if (item["debounce_ms"].is<int>()) out["debounce_ms"] = item["debounce_ms"].as<int>();
        }
    }

    if (source["keypads"].is<JsonArrayConst>()) {
        for (JsonObjectConst item : source["keypads"].as<JsonArrayConst>()) {
            auto out = keypads.add<JsonObject>();
            out["id"] = item["id"] | String("");
            if (item["rows"].is<JsonArrayConst>()) out["rows"] = item["rows"];
            if (item["cols"].is<JsonArrayConst>()) out["cols"] = item["cols"];
            if (item["keys"].is<JsonArrayConst>()) out["keys"] = item["keys"];
            if (item["debounce_ms"].is<int>()) out["debounce_ms"] = item["debounce_ms"].as<int>();
        }
    }

    if (!StorageManager::instance().writeJson(OF_INPUTS_PATH, toSave)) {
        sendError(request, 500, "Failed to save inputs");
        return;
    }

    InputManager::instance().requestReload();  // apply live — no reboot

    JsonDocument response;
    response["ok"] = true;
    response["restartRequired"] = false;
    response["message"] = "Inputs saved and applied.";
    sendJson(request, response);
}

void ApiServer::handleOutputsUpdate(AsyncWebServerRequest* request, const String& body) {
    JsonDocument source;
    if (deserializeJson(source, body)) {
        sendError(request, 400, "Invalid JSON");
        return;
    }

    // Require an explicit "outputs" array. Without this guard a body that lacks
    // the key (e.g. a misrouted /api/outputs/control request) would silently
    // persist an empty config and reboot — wiping every configured output.
    if (!source["outputs"].is<JsonArrayConst>()) {
        sendError(request, 400, "Missing 'outputs' array");
        return;
    }

    JsonDocument toSave;
    auto outputs = toSave["outputs"].to<JsonArray>();

    {
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
            if (item["gamma"].is<bool>()) out["gamma"] = item["gamma"].as<bool>();

            if (item["pin_r"].is<int>()) out["pin_r"] = item["pin_r"].as<int>();
            if (item["pin_g"].is<int>()) out["pin_g"] = item["pin_g"].as<int>();
            if (item["pin_b"].is<int>()) out["pin_b"] = item["pin_b"].as<int>();
            if (item["channel_r"].is<int>()) out["channel_r"] = item["channel_r"].as<int>();
            if (item["channel_g"].is<int>()) out["channel_g"] = item["channel_g"].as<int>();
            if (item["channel_b"].is<int>()) out["channel_b"] = item["channel_b"].as<int>();

            if (item["led_count"].is<int>()) out["led_count"] = item["led_count"].as<int>();
            if (item["brightness"].is<int>()) out["brightness"] = item["brightness"].as<int>();

            if (item["servo_min_us"].is<int>()) out["servo_min_us"] = item["servo_min_us"].as<int>();
            if (item["servo_max_us"].is<int>()) out["servo_max_us"] = item["servo_max_us"].as<int>();

            if (item["pin_dir"].is<int>()) out["pin_dir"] = item["pin_dir"].as<int>();
            if (item["pin_enable"].is<int>()) out["pin_enable"] = item["pin_enable"].as<int>();
            if (item["steps_per_rev"].is<int>()) out["steps_per_rev"] = item["steps_per_rev"].as<int>();
            if (item["max_step_hz"].is<int>()) out["max_step_hz"] = item["max_step_hz"].as<int>();
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

    if (!source["sensors"].is<JsonArrayConst>()) {
        sendError(request, 400, "Missing 'sensors' array");
        return;
    }

    JsonDocument toSave;
    auto sensors = toSave["sensors"].to<JsonArray>();

    {
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
            if (item["clock_pin"].is<int>()) out["clock_pin"] = item["clock_pin"].as<int>();
            if (item["cs_pin"].is<int>()) out["cs_pin"] = item["cs_pin"].as<int>();
            if (item["scale"].is<float>()) out["scale"] = item["scale"].as<float>();
            if (item["tare_offset"].is<int>()) out["tare_offset"] = item["tare_offset"].as<int>();
            if (item["offset"].is<float>()) out["offset"] = item["offset"].as<float>();
            // UART sensors (MH-Z19, PMS5003): wiring + bus.
            if (item["rx_pin"].is<int>()) out["rx_pin"] = item["rx_pin"].as<int>();
            if (item["tx_pin"].is<int>()) out["tx_pin"] = item["tx_pin"].as<int>();
            if (item["uart_num"].is<int>()) out["uart_num"] = item["uart_num"].as<int>();
            if (item["baud_rate"].is<int>()) out["baud_rate"] = item["baud_rate"].as<int>();

            // Generic I2C register map (#22): copy it through verbatim so a custom
            // chip definition round-trips through save/load.
            if (item["registers"].is<JsonArrayConst>()) {
                out["registers"] = item["registers"];
            }
        }
    }

    if (!StorageManager::instance().writeJson(OF_SENSORS_PATH, toSave)) {
        sendError(request, 500, "Failed to save sensors");
        return;
    }

    SensorManager::instance().requestReload();  // apply live — no reboot

    JsonDocument response;
    response["ok"] = true;
    response["restartRequired"] = false;
    response["message"] = "Sensors saved and applied.";
    sendJson(request, response);
}

void ApiServer::handleHaImportUpdate(AsyncWebServerRequest* request, const String& body) {
    JsonDocument source;
    if (deserializeJson(source, body)) {
        sendError(request, 400, "Invalid JSON");
        return;
    }
    // Require the array so a malformed request can't silently wipe the import set.
    if (!source["entities"].is<JsonArrayConst>()) {
        sendError(request, 400, "Missing 'entities' array");
        return;
    }

    JsonDocument toSave;
    auto entities = toSave["entities"].to<JsonArray>();
    for (JsonObjectConst item : source["entities"].as<JsonArrayConst>()) {
        const String entityId = item["entity_id"] | String("");
        // entity_id must be "<domain>.<object_id>"; skip anything malformed.
        if (entityId.indexOf('.') <= 0) continue;
        auto out = entities.add<JsonObject>();
        out["entity_id"]   = entityId;
        out["variable_id"] = item["variable_id"] | String("");
        out["type"]        = item["type"] | String("string");  // integer|float|boolean|string
        out["writable"]    = item["writable"] | false;
    }

    if (!StorageManager::instance().writeJson(OF_HA_IMPORT_PATH, toSave)) {
        sendError(request, 500, "Failed to save HA import");
        return;
    }

    JsonDocument response;
    response["ok"] = true;
    response["restartRequired"] = true;
    response["message"] = "HA import saved. Restarting to apply.";
    sendJson(request, response);
    scheduleRestart();
}

void ApiServer::handleDisplaysUpdate(AsyncWebServerRequest* request, const String& body) {
    JsonDocument source;
    if (deserializeJson(source, body)) {
        sendError(request, 400, "Invalid JSON");
        return;
    }

    bool displaysChanged = false;
    bool pagesChanged = false;

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

            // Address may arrive as a number (60) or a hex string ("0x3C") from the
            // UI — accept both so the field round-trips and arbitrary addresses work.
            JsonVariantConst addrVar = item["address"];
            uint8_t address = 0x3C;
            if (addrVar.is<const char*>()) {
                address = static_cast<uint8_t>(strtol(addrVar.as<const char*>(), nullptr, 0));
            } else if (addrVar.is<int>()) {
                address = static_cast<uint8_t>(addrVar.as<int>());
            }
            out["address"] = address;

            // I²C wiring (SSD1306/SH1106): pins and the sub-window mapping for small
            // panels (e.g. the C3's 0.42" 72x40, which needs col_offset 28 / com_pins
            // 0x12). Without these the bus falls back to board defaults and the panel
            // can't be configured — only persist when present so omitted = auto.
            if (item["sda_pin"].is<int>())     out["sda_pin"]     = item["sda_pin"].as<int>();
            if (item["scl_pin"].is<int>())     out["scl_pin"]     = item["scl_pin"].as<int>();
            if (item["col_offset"].is<int>())  out["col_offset"]  = item["col_offset"].as<int>();
            if (item["page_offset"].is<int>()) out["page_offset"] = item["page_offset"].as<int>();
            if (item["com_pins"].is<int>())    out["com_pins"]    = item["com_pins"].as<int>();

            if (item["rotation"].is<int>()) out["rotation"] = item["rotation"].as<int>();
            if (item["refresh_interval_ms"].is<int>()) out["refresh_interval_ms"] = item["refresh_interval_ms"].as<int>();
            if (item["page"].is<const char*>()) out["page"] = item["page"].as<const char*>();

            // Multi-screen rotation (F4): cycle pages on a timer in page_order.
            if (item["rotation_enabled"].is<bool>())     out["rotation_enabled"]     = item["rotation_enabled"].as<bool>();
            if (item["rotation_interval_ms"].is<int>())  out["rotation_interval_ms"] = item["rotation_interval_ms"].as<int>();
            if (item["page_order"].is<JsonArrayConst>()) out["page_order"]           = item["page_order"];
            if (item["reset_pin"].is<int>()) out["reset_pin"] = item["reset_pin"].as<int>();
            if (item["contrast"].is<int>()) out["contrast"] = item["contrast"].as<int>();
            // Brightness + night-mode dimming (day level 0-255; window in minutes
            // past local midnight; night level or full blank). Only persist when
            // present so an omitted field keeps the DisplayConfig default.
            if (item["brightness"].is<int>())       out["brightness"]       = item["brightness"].as<int>();
            if (item["night_enabled"].is<bool>())   out["night_enabled"]    = item["night_enabled"].as<bool>();
            if (item["night_start_min"].is<int>())  out["night_start_min"]  = item["night_start_min"].as<int>();
            if (item["night_end_min"].is<int>())    out["night_end_min"]    = item["night_end_min"].as<int>();
            if (item["night_brightness"].is<int>()) out["night_brightness"] = item["night_brightness"].as<int>();
            if (item["night_blank"].is<bool>())     out["night_blank"]      = item["night_blank"].as<bool>();
            if (item["cs_pin"].is<int>()) out["cs_pin"] = item["cs_pin"].as<int>();
            if (item["dc_pin"].is<int>()) out["dc_pin"] = item["dc_pin"].as<int>();
            if (item["mosi_pin"].is<int>()) out["mosi_pin"] = item["mosi_pin"].as<int>();
            if (item["sck_pin"].is<int>()) out["sck_pin"] = item["sck_pin"].as<int>();
            if (item["spi_frequency"].is<int>()) out["spi_frequency"] = item["spi_frequency"].as<int>();
            // Backlight pin for integrated TFT panels (S3-GEEK/-BOX) — without it
            // the panel inits but stays dark. Only persist when present so an
            // omitted field means "no backlight pin / always-on".
            if (item["bl_pin"].is<int>()) out["bl_pin"] = item["bl_pin"].as<int>();
            if (item["bl_active_low"].is<bool>()) out["bl_active_low"] = item["bl_active_low"].as<bool>();
            if (item["rx_pin"].is<int>()) out["rx_pin"] = item["rx_pin"].as<int>();
            if (item["tx_pin"].is<int>()) out["tx_pin"] = item["tx_pin"].as<int>();
            if (item["baud_rate"].is<int>()) out["baud_rate"] = item["baud_rate"].as<int>();
            if (item["uart_num"].is<int>()) out["uart_num"] = item["uart_num"].as<int>();
        }

        if (!StorageManager::instance().writeJson(OF_DISPLAYS_PATH, cfgDoc)) {
            sendError(request, 500, "Failed to save displays");
            return;
        }
        displaysChanged = true;
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
            // Page background colour (colour panels) — hex string "#RRGGBB".
            if (!item["bg"].isNull()) pageDoc["bg"] = item["bg"];

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
                    // Button widget: the action id fired on tap. Only persisted when
                    // set so non-button widgets round-trip unchanged.
                    if (!w["action"].isNull()) out["action"] = w["action"] | String("");
                    // Interactive widgets: nav destination / cycle list + stepper step.
                    if (!w["target"].isNull()) out["target"] = w["target"] | String("");
                    if (!w["step"].isNull())   out["step"]   = w["step"].as<float>();

                    // Geometry/style for the richer widget types (rect/line/circle/bar/
                    // led/icon) and per-widget colour + alignment. Persist only when
                    // present so simple text pages stay compact and round-trip cleanly.
                    if (w["w"].is<int>())        out["w"]      = w["w"].as<int>();
                    if (w["h"].is<int>())        out["h"]      = w["h"].as<int>();
                    if (w["align"].is<int>())    out["align"]  = w["align"].as<int>();
                    if (!w["color"].isNull())    out["color"]  = w["color"];   // "#RRGGBB"
                    if (!w["bg"].isNull())       out["bg"]     = w["bg"];
                    if (w["filled"].is<bool>())  out["filled"] = w["filled"].as<bool>();
                    if (!w["min"].isNull())      out["min"]    = w["min"].as<float>();
                    if (!w["max"].isNull())      out["max"]    = w["max"].as<float>();
                }
            }

            const String path = String(OF_DISPLAY_PAGES_PATH) + "/" + pageId + ".json";
            if (!StorageManager::instance().writeJson(path, pageDoc)) {
                sendError(request, 500, "Failed to save display page " + pageId);
                return;
            }
        }
        pagesChanged = true;
    }

    if (!displaysChanged && !pagesChanged) {
        sendError(request, 400, "Payload must include displays or pages");
        return;
    }

    // Apply live — no reboot. A display *hardware* change needs a full provider
    // rebuild; a page-content-only change (the common live-edit case) takes the
    // light path that keeps the providers and just re-renders — avoiding the
    // canvas/SPI reallocation churn that fragments the heap (see
    // DisplayManager::requestReload / requestPagesReload).
    if (displaysChanged) {
        DisplayManager::instance().requestReload();
    } else {
        DisplayManager::instance().requestPagesReload();
    }

    JsonDocument response;
    response["ok"] = true;
    response["restartRequired"] = false;
    response["message"] = "Displays/pages saved and applied.";
    sendJson(request, response);
}

void ApiServer::sendModules(AsyncWebServerRequest* request) const {
    JsonDocument doc;
    auto arr = doc["modules"].to<JsonArray>();
    for (const auto& m : ModuleManager::instance().modules()) {
        auto obj = arr.add<JsonObject>();
        obj["address"] = m.address;
        obj["type"] = m.typeLabel;
        if (m.chipModel.length())       obj["chip_model"]       = m.chipModel;
        if (m.suggestedDriver.length()) obj["suggested_driver"] = m.suggestedDriver;
        obj["online"] = m.online;
        obj["last_seen_ms"] = m.lastSeenMs;
    }
    doc["count"] = ModuleManager::instance().modules().size();
    sendJson(request, doc);
}

void ApiServer::sendHardware(AsyncWebServerRequest* request) const {
    JsonDocument doc;

    // Board / SoC self-report
    fillHardwareInfo(doc["board"].to<JsonObject>());

    // Identified I2C bus scan (mirrors /api/modules but framed as a discovery view)
    auto i2c = doc["i2c"].to<JsonObject>();
    auto arr = i2c["devices"].to<JsonArray>();
    for (const auto& m : ModuleManager::instance().modules()) {
        if (!m.online) continue;
        auto obj = arr.add<JsonObject>();
        obj["address"]     = m.address;
        obj["address_hex"] = "0x" + String(m.address, HEX);
        obj["type"]        = m.typeLabel;
        if (m.chipModel.length())       obj["chip_model"]       = m.chipModel;
        if (m.suggestedDriver.length()) obj["suggested_driver"] = m.suggestedDriver;
        // Adoptable = we have a sensor driver that maps to this chip.
        obj["adoptable"]   = m.suggestedDriver.length() > 0;
    }
    i2c["error_count"] = ModuleManager::instance().i2cErrorCount();
    sendJson(request, doc);
}

void ApiServer::handleHardwareAdopt(AsyncWebServerRequest* request, const String& body) {
    // Optional body: {"addresses":[118, 104]} restricts adoption to those I2C
    // addresses. An empty/missing list adopts every adoptable detected device.
    JsonDocument req;
    if (body.length() && deserializeJson(req, body)) {
        sendError(request, 400, "Invalid JSON");
        return;
    }

    std::vector<uint8_t> filter;
    bool haveFilter = false;
    if (req["addresses"].is<JsonArrayConst>()) {
        haveFilter = true;
        for (JsonVariantConst v : req["addresses"].as<JsonArrayConst>()) {
            filter.push_back(static_cast<uint8_t>(v.as<unsigned>()));
        }
    }
    auto wanted = [&](uint8_t addr) {
        if (!haveFilter) return true;
        return std::find(filter.begin(), filter.end(), addr) != filter.end();
    };

    // Load the current sensor config so adoption is additive and idempotent.
    JsonDocument doc;
    StorageManager::instance().readJson(OF_SENSORS_PATH, doc);
    if (!doc["sensors"].is<JsonArray>()) {
        doc["sensors"].to<JsonArray>();
    }
    JsonArray sensors = doc["sensors"].as<JsonArray>();

    auto alreadyPresent = [&](const String& type, uint8_t addr) {
        for (JsonObjectConst s : sensors) {
            const String t = s["type"] | String("");
            const uint8_t a = s["address"] | 0;
            if (t == type && a == addr) return true;
        }
        return false;
    };

    JsonDocument response;
    auto added   = response["added"].to<JsonArray>();
    auto skipped = response["skipped"].to<JsonArray>();

    for (const auto& m : ModuleManager::instance().modules()) {
        if (!m.online || !m.suggestedDriver.length()) continue;
        if (!wanted(m.address)) continue;

        if (alreadyPresent(m.suggestedDriver, m.address)) {
            auto s = skipped.add<JsonObject>();
            s["address"] = m.address;
            s["type"]    = m.suggestedDriver;
            s["reason"]  = "already configured";
            continue;
        }

        const String id = m.suggestedDriver + "_" + String(m.address, HEX);
        auto entry = sensors.add<JsonObject>();
        entry["id"]               = id;
        entry["type"]             = m.suggestedDriver;
        entry["enabled"]          = true;
        entry["poll_interval_ms"] = 5000;
        entry["variable_prefix"]  = "";
        entry["address"]          = m.address;
        entry["pin"]              = 0;

        auto a = added.add<JsonObject>();
        a["id"]      = id;
        a["address"] = m.address;
        a["type"]    = m.suggestedDriver;
    }

    if (added.size() == 0) {
        response["ok"] = true;
        response["restartRequired"] = false;
        response["message"] = "No new devices to adopt";
        sendJson(request, response);
        return;
    }

    if (!StorageManager::instance().writeJson(OF_SENSORS_PATH, doc)) {
        sendError(request, 500, "Failed to save sensors");
        return;
    }

    SensorManager::instance().requestReload();  // apply live — no reboot

    response["ok"] = true;
    response["restartRequired"] = false;
    response["message"] = String(added.size()) + " sensor(s) adopted and applied.";
    sendJson(request, response);
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
        auto params = obj["params"].to<JsonArray>();
        for (const auto& p : macro.params) {
            auto po = params.add<JsonObject>();
            po["variable_id"] = p.variableId;
            po["value"]       = p.value;
        }
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

    // Parse via ActionEngine's own parser — the one loadActions() uses — so the
    // REST body and the stored file share a single source of truth. (A mirrored
    // copy here once collapsed unknown step types to Delay when it went stale.)
    bool updated = false;
    if (doc["actions"].is<JsonArrayConst>()) {
        for (JsonObjectConst item : doc["actions"].as<JsonArrayConst>()) {
            ActionConfig action;
            if (!ActionEngine::parseActionJson(item, action)) continue;
            ActionEngine::instance().registerAction(action);
            updated = true;
        }
    } else if (doc["id"].is<const char*>()) {
        ActionConfig action;
        if (ActionEngine::parseActionJson(doc.as<JsonObjectConst>(), action)) {
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
        if (item["params"].is<JsonArrayConst>()) {
            for (JsonObjectConst p : item["params"].as<JsonArrayConst>()) {
                MacroParam mp;
                mp.variableId = p["variable_id"] | String("");
                mp.value      = p["value"] | String("");
                if (mp.variableId.length()) macro.params.push_back(mp);
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

void ApiServer::handleActionDelete(AsyncWebServerRequest* request, const String& actionId) {
    if (!actionId.length()) {
        sendError(request, 400, "Missing action id");
        return;
    }
    if (!ActionEngine::instance().removeAction(actionId)) {
        sendError(request, 404, "Action not found");
        return;
    }
    if (!ActionEngine::instance().saveActions()) {
        sendError(request, 500, "Failed to save actions");
        return;
    }
    sendActions(request);
}

void ApiServer::handleMacroDelete(AsyncWebServerRequest* request, const String& macroId) {
    if (!macroId.length()) {
        sendError(request, 400, "Missing macro id");
        return;
    }
    if (!MacroManager::instance().removeMacro(macroId)) {
        sendError(request, 404, "Macro not found");
        return;
    }
    if (!MacroManager::instance().saveMacros()) {
        sendError(request, 500, "Failed to save macros");
        return;
    }
    sendMacros(request);
}

void ApiServer::handleActionSimulate(AsyncWebServerRequest* request, const String& body) {
    JsonDocument doc;
    if (deserializeJson(doc, body)) { sendError(request, 400, "Invalid JSON"); return; }
    const String id = doc["id"] | String("");
    if (!id.length()) { sendError(request, 400, "Missing action id"); return; }

    std::vector<String> log;
    String error;
    const bool ok = ActionEngine::instance().simulateAction(id, log, error);

    JsonDocument out;
    out["id"] = id;
    out["ok"] = ok;
    if (!ok && error.length()) out["error"] = error;
    auto steps = out["steps"].to<JsonArray>();
    for (const auto& line : log) steps.add(line);
    sendJson(request, out);
}

// ── Scene handlers (#39) ──────────────────────────────────────────────────────

void ApiServer::sendScenes(AsyncWebServerRequest* request) const {
    JsonDocument doc;
    auto arr = doc["scenes"].to<JsonArray>();
    SceneManager::instance().fillListJson(arr);
    doc["count"] = arr.size();
    sendJson(request, doc);
}

void ApiServer::handleSceneCapture(AsyncWebServerRequest* request, const String& body) {
    JsonDocument doc;
    if (deserializeJson(doc, body)) { sendError(request, 400, "Invalid JSON"); return; }
    const String name = doc["name"] | String("");
    if (!name.length()) { sendError(request, 400, "Missing scene name"); return; }
    if (!SceneManager::instance().capture(name)) {
        sendError(request, 500, "Failed to capture scene (storage error or scene limit reached)");
        return;
    }
    sendScenes(request);
}

void ApiServer::handleSceneRestore(AsyncWebServerRequest* request, const String& body) {
    JsonDocument doc;
    if (deserializeJson(doc, body)) { sendError(request, 400, "Invalid JSON"); return; }
    const String name = doc["name"] | String("");
    if (!name.length()) { sendError(request, 400, "Missing scene name"); return; }
    String error;
    if (!SceneManager::instance().restore(name, error)) {
        sendError(request, 404, error.length() ? error : "Failed to restore scene");
        return;
    }
    sendScenes(request);
}

void ApiServer::handleSceneDelete(AsyncWebServerRequest* request, const String& name) {
    if (!name.length()) { sendError(request, 400, "Missing scene name"); return; }
    if (!SceneManager::instance().remove(name)) {
        sendError(request, 404, "Scene not found");
        return;
    }
    sendScenes(request);
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
        String path = f;
        if (!path.startsWith("/")) {
            path = "/templates/" + f;
        }
        JsonDocument tmpl;
        if (!StorageManager::instance().readJson(path, tmpl)) continue;
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

// ── Filesystem browser ─────────────────────────────────────────────────────────

void ApiServer::sendFsStat(AsyncWebServerRequest* request) const {
    const StorageInfo si = StorageManager::instance().info();
    JsonDocument doc;
    doc["total"] = si.total;
    doc["used"]  = si.used;
    doc["free"]  = si.free;
    sendJson(request, doc);
}

void ApiServer::sendFsSelfTest(AsyncWebServerRequest* request) const {
    JsonDocument result;
    auto steps = result["steps"].to<JsonArray>();
    auto addStep = [&](const char* name, bool ok, const String& detail) {
        auto s = steps.add<JsonObject>();
        s["name"]   = name;
        s["ok"]     = ok;
        if (detail.length()) s["detail"] = detail;
    };

    auto& storage = StorageManager::instance();
    const char* testPath = "/._selftest.json";
    bool ok = true;

    // 1. Write a file with a known marker and a per-run nonce.
    const uint32_t nonce = millis();
    JsonDocument w;
    w["marker"] = "openframe-selftest";
    w["nonce"]  = nonce;
    const bool wrote = storage.writeJson(testPath, w);
    addStep("write", wrote, wrote ? String("") : "writeJson reported failure");
    ok = ok && wrote;

    // 2. Read it back and verify the contents survived the round-trip.
    bool verified = false;
    String verifyDetail;
    if (wrote) {
        JsonDocument r;
        if (storage.readJson(testPath, r)) {
            const bool markerOk = (r["marker"] | String("")) == "openframe-selftest";
            const bool nonceOk  = (r["nonce"] | 0u) == nonce;
            verified = markerOk && nonceOk;
            if (!verified) verifyDetail = "content mismatch after read-back";
        } else {
            verifyDetail = "readJson failed";
        }
    } else {
        verifyDetail = "skipped (write failed)";
    }
    addStep("read-back", verified, verifyDetail);
    ok = ok && verified;

    // 3. Clean up the temp file.
    const bool deleted = storage.remove(testPath);
    addStep("cleanup", deleted, deleted ? String("") : "remove failed");
    ok = ok && deleted;

    result["ok"] = ok;
    const StorageInfo si = storage.info();
    result["free"]  = si.free;
    result["total"] = si.total;
    sendJson(request, result);
}

void ApiServer::sendFsList(AsyncWebServerRequest* request) const {
    String path = request->hasParam("path") ? request->getParam("path")->value() : String("/");
    if (path.isEmpty()) path = "/";
    if (path.length() > 1 && path.endsWith("/")) path.remove(path.length() - 1);
    if (!isSafeFsPath(path)) {
        sendError(request, 400, "Invalid path");
        return;
    }

    JsonDocument doc;
    doc["path"] = path;
    auto entries = doc["entries"].to<JsonArray>();
    const String prefix = (path == "/") ? String("/") : (path + "/");
    for (const auto& e : StorageManager::instance().listEntries(path)) {
        auto obj = entries.add<JsonObject>();
        obj["name"] = e.name;
        obj["path"] = prefix + e.name;
        obj["type"] = e.isDir ? "dir" : "file";
        obj["size"] = e.size;
    }
    doc["count"] = entries.size();
    sendJson(request, doc);
}

void ApiServer::sendFsDownload(AsyncWebServerRequest* request) const {
    const String path = request->hasParam("path") ? request->getParam("path")->value() : String("");
    if (!isSafeFsPath(path)) {
        sendError(request, 400, "Invalid path");
        return;
    }
    if (!StorageManager::instance().exists(path)) {
        sendError(request, 404, "Not found");
        return;
    }

    // The download=true overload makes AsyncFileResponse emit
    // Content-Disposition: attachment; filename="…" itself. Adding our own here
    // too produced TWO Content-Disposition headers, which Chrome rejects outright
    // (net::ERR_RESPONSE_HEADERS_MULTIPLE_CONTENT_DISPOSITION) — the file never
    // downloads. Let the library own that header.
    auto* response = request->beginResponse(LittleFS, path, "application/octet-stream", true);
    response->addHeader("Cache-Control", "no-store");
    request->send(response);
}

void ApiServer::handleFsDelete(AsyncWebServerRequest* request) const {
    const String path = request->hasParam("path") ? request->getParam("path")->value() : String("");
    if (!isSafeFsPath(path) || path == "/") {
        // Refuse "/" outright — a recursive delete there would wipe /www too.
        sendError(request, 400, "Invalid path");
        return;
    }
    if (isProtectedFsPath(path)) {
        sendError(request, 403, "Path is protected");
        return;
    }
    if (!StorageManager::instance().exists(path)) {
        sendError(request, 404, "Not found");
        return;
    }
    // removeRecursive handles both a plain file and a directory (and its
    // contents). The only protected tree, /www, is a top-level directory and is
    // rejected above, so it can never be reached as a child of a deleted dir.
    if (!StorageManager::instance().removeRecursive(path)) {
        sendError(request, 500, "Delete failed");
        return;
    }
    JsonDocument doc;
    doc["ok"]   = true;
    doc["path"] = path;
    sendJson(request, doc);
}

void ApiServer::handleFsMkdir(AsyncWebServerRequest* request) const {
    const String path = request->hasParam("path") ? request->getParam("path")->value() : String("");
    if (!isSafeFsPath(path)) {
        sendError(request, 400, "Invalid path");
        return;
    }
    if (isProtectedFsPath(path)) {
        sendError(request, 403, "Path is protected");
        return;
    }
    if (StorageManager::instance().exists(path)) {
        sendError(request, 409, "Already exists");
        return;
    }
    if (!StorageManager::instance().mkdirs(path)) {
        sendError(request, 500, "mkdir failed");
        return;
    }
    JsonDocument doc;
    doc["ok"]   = true;
    doc["path"] = path;
    sendJson(request, doc);
}

void ApiServer::handleFsRename(AsyncWebServerRequest* request, const String& body) const {
    JsonDocument doc;
    if (deserializeJson(doc, body)) {
        sendError(request, 400, "Invalid JSON");
        return;
    }
    const String from = doc["from"] | String("");
    const String to   = doc["to"]   | String("");
    if (!isSafeFsPath(from) || !isSafeFsPath(to)) {
        sendError(request, 400, "Invalid path");
        return;
    }
    if (isProtectedFsPath(from) || isProtectedFsPath(to)) {
        sendError(request, 403, "Path is protected");
        return;
    }
    if (!StorageManager::instance().exists(from)) {
        sendError(request, 404, "Source not found");
        return;
    }
    if (StorageManager::instance().exists(to)) {
        sendError(request, 409, "Target already exists");
        return;
    }

    // Ensure the destination's parent directory exists before moving.
    int slash = to.lastIndexOf('/');
    if (slash > 0) {
        const String dir = to.substring(0, slash);
        if (!StorageManager::instance().exists(dir)) {
            StorageManager::instance().mkdirs(dir);
        }
    }

    if (!StorageManager::instance().rename(from, to)) {
        sendError(request, 500, "Rename failed");
        return;
    }
    JsonDocument resp;
    resp["ok"]   = true;
    resp["from"] = from;
    resp["to"]   = to;
    sendJson(request, resp);
}

void ApiServer::handleFsUpload(AsyncWebServerRequest* request, const String& path, const String& body) {
    if (!isSafeFsPath(path)) {
        sendError(request, 400, "Invalid path");
        return;
    }
    if (isProtectedFsPath(path)) {
        sendError(request, 403, "Path is protected");
        return;
    }

    // JSON files must be valid JSON — refuse to persist a file that would fail
    // to parse on next boot.
    if (path.endsWith(".json")) {
        JsonDocument validate;
        if (deserializeJson(validate, body)) {
            sendError(request, 400, "Body is not valid JSON");
            return;
        }
    }

    // Ensure the parent directory exists.
    int slash = path.lastIndexOf('/');
    if (slash > 0) {
        const String dir = path.substring(0, slash);
        if (!StorageManager::instance().exists(dir)) {
            StorageManager::instance().mkdirs(dir);
        }
    }

    if (!StorageManager::instance().writeRaw(path, body)) {
        sendError(request, 500, "Write failed");
        return;
    }
    // Overwriting config.json directly bypasses ConfigManager::save(), so refresh
    // the NVS config mirror too — otherwise a boot with unreadable LittleFS would
    // revert to the old config (body already validated as JSON above).
    if (path == OF_CONFIG_PATH) {
        JsonDocument cfg;
        if (!deserializeJson(cfg, body)) {
            ConfigManager::instance().syncNvsBackup(cfg);
        }
    }
    JsonDocument doc;
    doc["ok"]    = true;
    doc["path"]  = path;
    doc["bytes"] = body.length();
    sendJson(request, doc);
}
