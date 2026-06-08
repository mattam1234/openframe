#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
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
    void sendWifiScan(AsyncWebServerRequest* request) const;
    void sendVariables(AsyncWebServerRequest* request) const;
    void sendLogs(AsyncWebServerRequest* request) const;
    void sendOtaStatus(AsyncWebServerRequest* request) const;
    void sendInputs(AsyncWebServerRequest* request) const;
    void sendOutputs(AsyncWebServerRequest* request) const;
    void sendSensors(AsyncWebServerRequest* request) const;
    void sendDisplays(AsyncWebServerRequest* request) const;
    void sendDisplayPages(AsyncWebServerRequest* request) const;
    void sendActions(AsyncWebServerRequest* request) const;
    void sendMacros(AsyncWebServerRequest* request) const;
    void sendModules(AsyncWebServerRequest* request) const;
    void sendProfiles(AsyncWebServerRequest* request) const;
    void sendTemplates(AsyncWebServerRequest* request) const;
    void sendTemplateById(AsyncWebServerRequest* request, const String& templateId) const;
    void sendNotifications(AsyncWebServerRequest* request) const;

    // Filesystem browser
    void sendFsStat(AsyncWebServerRequest* request) const;
    void sendFsList(AsyncWebServerRequest* request) const;
    void sendFsDownload(AsyncWebServerRequest* request) const;
    void handleFsDelete(AsyncWebServerRequest* request) const;
    void handleFsUpload(AsyncWebServerRequest* request, const String& path, const String& body);

    void handleConfigUpdate(AsyncWebServerRequest* request, const String& body);
    void handleVariablesUpdate(AsyncWebServerRequest* request, const String& body);
    void handleInputsUpdate(AsyncWebServerRequest* request, const String& body);
    void handleOutputsUpdate(AsyncWebServerRequest* request, const String& body);
    void handleSensorsUpdate(AsyncWebServerRequest* request, const String& body);
    void handleDisplaysUpdate(AsyncWebServerRequest* request, const String& body);
    void handleActionsUpdate(AsyncWebServerRequest* request, const String& body);
    void handleMacrosUpdate(AsyncWebServerRequest* request, const String& body);
    void handleProfileCreate(AsyncWebServerRequest* request, const String& body);
    void handleProfileActivate(AsyncWebServerRequest* request, const String& body);
    void handleProfileDelete(AsyncWebServerRequest* request, const String& profileId);
    void handleTemplateExport(AsyncWebServerRequest* request, const String& body);
    void handleTemplateImport(AsyncWebServerRequest* request, const String& body);

    void handleWebSocketMessage(AsyncWebSocketClient* client, const String& message);
    void sendInitialState(AsyncWebSocketClient* client) const;
    void publishHealthUpdate();
    void onEvent(const Event& event);
    void onLogEntry(const LogEntry& entry);

    void sendJson(AsyncWebServerRequest* request, const JsonDocument& doc, int status = 200) const;
    void sendError(AsyncWebServerRequest* request, int status, const String& message) const;
    void scheduleRestart(uint32_t delayMs = 1500);
    void sendFrame(AsyncWebSocketClient* client, const char* type, const JsonDocument& payload) const;
    void sendRawFrame(AsyncWebSocketClient* client, const char* type, const String& rawPayloadJson) const;
    void broadcastFrame(const char* type, const JsonDocument& payload);
    void broadcastRawFrame(const char* type, const String& rawPayloadJson);

    String buildStatusJson() const;
    String buildVariablesJson() const;
    String buildLogsJson() const;
    bool applyVariableUpdate(const JsonVariantConst& item, String& error) const;

    AsyncWebSocket _ws;
    bool           _started = false;
    bool           _subscribed = false;
    bool           _restartPending = false;
    uint32_t       _restartAtMs = 0;
    uint32_t       _lastHealthPublishMs = 0;

    static constexpr uint32_t HEALTH_PUBLISH_INTERVAL_MS = 5000;
    static constexpr const char* TAG = "ApiServer";
};
