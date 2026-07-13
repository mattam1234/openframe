#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <set>
#include "../core/EventBus.h"
#include "../core/Logger.h"

class ApiServer {
public:
    static ApiServer& instance();

    void begin(AsyncWebServer& server);
    void loop();

    // Returns true when the request is allowed: always true if no api_token is
    // configured (LAN-trusted default), else requires a matching Bearer header.
    // Sends a 401 itself when it returns false. Public so the shared
    // body-buffering POST helper can gate before invoking a handler.
    bool requireAuth(AsyncWebServerRequest* request) const;

private:
    ApiServer();

    void registerRoutes(AsyncWebServer& server);
    void registerWebSocket(AsyncWebServer& server);

    void sendStatus(AsyncWebServerRequest* request) const;
    void sendConfig(AsyncWebServerRequest* request) const;
    void sendWifiScan(AsyncWebServerRequest* request) const;
    void sendVariables(AsyncWebServerRequest* request) const;
    void sendScreens(AsyncWebServerRequest* request) const;
#if defined(OF_ENABLE_XPT2046_TOUCH)
    void sendTouchCalibration(AsyncWebServerRequest* request) const;
    void handleTouchUpdate(AsyncWebServerRequest* request, const String& body);
#endif
    void sendLogs(AsyncWebServerRequest* request) const;
    void sendOtaStatus(AsyncWebServerRequest* request) const;
    void sendInputs(AsyncWebServerRequest* request) const;
    void sendOutputs(AsyncWebServerRequest* request) const;
    void sendOutputsState(AsyncWebServerRequest* request) const;
    void sendSensors(AsyncWebServerRequest* request) const;
    void sendHaImport(AsyncWebServerRequest* request) const;
    void sendDisplays(AsyncWebServerRequest* request) const;
    void sendDisplayPages(AsyncWebServerRequest* request) const;
    void sendImages(AsyncWebServerRequest* request) const;
    void handleImageDelete(AsyncWebServerRequest* request);
    void handleImageUploadChunk(AsyncWebServerRequest* request, uint8_t* data,
                                size_t len, size_t index, size_t total);
    void sendActions(AsyncWebServerRequest* request) const;
    void sendMacros(AsyncWebServerRequest* request) const;
    void sendScenes(AsyncWebServerRequest* request) const;
    void sendMetrics(AsyncWebServerRequest* request) const;
    void sendSelfTest(AsyncWebServerRequest* request) const;
    void sendNetDiag(AsyncWebServerRequest* request) const;
    void sendModules(AsyncWebServerRequest* request) const;
    void sendHardware(AsyncWebServerRequest* request) const;
    void sendProfiles(AsyncWebServerRequest* request) const;
    void sendTemplates(AsyncWebServerRequest* request) const;
    void sendTemplateById(AsyncWebServerRequest* request, const String& templateId) const;
    void sendNotifications(AsyncWebServerRequest* request) const;

    // Filesystem browser
    void sendFsStat(AsyncWebServerRequest* request) const;
    void sendFsSelfTest(AsyncWebServerRequest* request) const;
    void sendFsList(AsyncWebServerRequest* request) const;
    void sendFsDownload(AsyncWebServerRequest* request) const;
    void handleFsDelete(AsyncWebServerRequest* request) const;
    void handleFsMkdir(AsyncWebServerRequest* request) const;
    void handleFsRename(AsyncWebServerRequest* request, const String& body) const;
    void handleFsUpload(AsyncWebServerRequest* request, const String& path, const String& body);

    void handleConfigUpdate(AsyncWebServerRequest* request, const String& body);
    // Config backup/restore slots (one-tap rollback).
    void sendConfigBackups(AsyncWebServerRequest* request) const;
    void handleConfigBackupCreate(AsyncWebServerRequest* request);
    void handleConfigRestore(AsyncWebServerRequest* request, const String& body);
    void handleConfigBackupDelete(AsyncWebServerRequest* request);
    String createConfigBackup() const;   // snapshot /config.json into a new slot
    void   pruneConfigBackups(size_t keep) const;
    void handleVariablesUpdate(AsyncWebServerRequest* request, const String& body);
    void handleInputsUpdate(AsyncWebServerRequest* request, const String& body);
    void handleOutputsUpdate(AsyncWebServerRequest* request, const String& body);
    void handleOutputsControl(AsyncWebServerRequest* request, const String& body);
    void handleSensorsUpdate(AsyncWebServerRequest* request, const String& body);
    void handleHaImportUpdate(AsyncWebServerRequest* request, const String& body);
    void handleHardwareAdopt(AsyncWebServerRequest* request, const String& body);
    void handleDisplaysUpdate(AsyncWebServerRequest* request, const String& body);
    void handleActionsUpdate(AsyncWebServerRequest* request, const String& body);
    void handleActionDelete(AsyncWebServerRequest* request, const String& actionId);
    void handleMacrosUpdate(AsyncWebServerRequest* request, const String& body);
    void handleMacroDelete(AsyncWebServerRequest* request, const String& macroId);
    void handleActionSimulate(AsyncWebServerRequest* request, const String& body);
    void handleSceneCapture(AsyncWebServerRequest* request, const String& body);
    void handleSceneRestore(AsyncWebServerRequest* request, const String& body);
    void handleSceneDelete(AsyncWebServerRequest* request, const String& name);
    void handleProfileCreate(AsyncWebServerRequest* request, const String& body);
    void handleProfileActivate(AsyncWebServerRequest* request, const String& body);
    void handleProfileDelete(AsyncWebServerRequest* request, const String& profileId);
    void handleTemplateExport(AsyncWebServerRequest* request, const String& body);
    void handleTemplateImport(AsyncWebServerRequest* request, const String& body);

    void handleWebSocketMessage(AsyncWebSocketClient* client, const String& message);
    // Whether this WS client has presented the api_token via an `auth` frame.
    // Always true when no token is configured (LAN-trusted default).
    bool wsClientAuthed(AsyncWebSocketClient* client) const;
    void forgetWsClient(AsyncWebSocketClient* client);  // on disconnect
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

    // True only when there's enough contiguous heap to safely allocate a WebSocket
    // send buffer. AsyncWebSocket::textAll / client->text() allocate a buffer the
    // size of the frame and THROW on failure — which aborts the whole device. Under
    // memory pressure we'd rather silently drop a telemetry frame, so every WS send
    // is gated on this. Floors are generous because the heap is fragmented, not just
    // low (a small largest-block with plenty of free heap still fails a big alloc).
    bool wsSendSafe() const;
    // Frame-size-aware variant: wsSendSafe() plus room for THIS frame's buffer.
    // wsSendSafe() alone isn't enough — it can pass and a multi-KB frame still
    // fail (observed: bad_alloc → abort in sendInitialState's log snapshot while
    // the browser was re-streaming assets after a reboot).
    bool wsCanSend(size_t frameLen) const;
    static constexpr uint32_t WS_MIN_FREE_HEAP   = 24000;  // bytes
    static constexpr uint32_t WS_MIN_ALLOC_BLOCK = 8000;   // bytes (largest contiguous)
    static constexpr uint32_t WS_SEND_HEADROOM   = 4096;   // slack beyond the frame itself

    String buildStatusJson() const;
    // Populate `doc` directly (no intermediate String). Callers that send over
    // HTTP use this and serialize once; the String wrappers below exist only for
    // the WebSocket-frame paths that genuinely need a String.
    void   buildVariablesDoc(JsonDocument& doc) const;
    String buildVariablesJson() const;
    // maxCount > 0 limits the result to the most recent maxCount entries
    // (used for the bounded WebSocket connect snapshot); 0 returns the full ring.
    void   buildLogsDoc(JsonDocument& doc, size_t maxCount = 0) const;
    String buildLogsJson(size_t maxCount = 0) const;
    bool applyVariableUpdate(const JsonVariantConst& item, String& error) const;

    AsyncWebSocket _ws;
    // WS clients that authenticated via an `auth` frame (#75). Browsers can't set
    // an Authorization header on the WS upgrade, so state-changing WS messages are
    // gated by a first-message token handshake instead. Cleared per disconnect.
    std::set<uint32_t> _authedWsClients;
    bool           _started = false;
    bool           _subscribed = false;
    bool           _restartPending = false;
    uint32_t       _restartAtMs = 0;
    uint32_t       _lastHealthPublishMs = 0;

    static constexpr uint32_t HEALTH_PUBLISH_INTERVAL_MS = 5000;
    static constexpr const char* TAG = "ApiServer";
};
