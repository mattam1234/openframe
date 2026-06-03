#pragma once

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include "../core/EventBus.h"
#include "../core/Logger.h"

class ApiServer {
public:
    static ApiServer& instance();

    void begin(AsyncWebServer& server);
    void loop();

private:
    ApiServer();

    void registerRoutes(AsyncWebServer& server);
    void registerWebSocket(AsyncWebServer& server);

    void sendStatus(AsyncWebServerRequest* request) const;
    void sendConfig(AsyncWebServerRequest* request) const;
    void sendVariables(AsyncWebServerRequest* request) const;
    void sendLogs(AsyncWebServerRequest* request) const;
    void sendOtaStatus(AsyncWebServerRequest* request) const;

    void handleConfigUpdate(AsyncWebServerRequest* request, const String& body);
    void handleVariablesUpdate(AsyncWebServerRequest* request, const String& body);

    void handleWebSocketMessage(AsyncWebSocketClient* client, const String& message);
    void sendInitialState(AsyncWebSocketClient* client) const;
    void publishHealthUpdate();
    void onEvent(const Event& event);
    void onLogEntry(const LogEntry& entry);

    void sendJson(AsyncWebServerRequest* request, const JsonDocument& doc, int status = 200) const;
    void sendError(AsyncWebServerRequest* request, int status, const String& message) const;
    void sendFrame(AsyncWebSocketClient* client, const char* type, const JsonDocument& payload) const;
    void sendRawFrame(AsyncWebSocketClient* client, const char* type, const String& rawPayloadJson) const;
    void broadcastFrame(const char* type, const JsonDocument& payload) const;
    void broadcastRawFrame(const char* type, const String& rawPayloadJson) const;

    String buildStatusJson() const;
    String buildVariablesJson() const;
    String buildLogsJson() const;
    bool applyVariableUpdate(const JsonVariantConst& item, String& error) const;

    AsyncWebSocket _ws;
    bool           _started = false;
    bool           _subscribed = false;
    uint32_t       _lastHealthPublishMs = 0;

    static constexpr uint32_t HEALTH_PUBLISH_INTERVAL_MS = 5000;
    static constexpr const char* TAG = "ApiServer";
};
