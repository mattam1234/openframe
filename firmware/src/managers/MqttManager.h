#pragma once

#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFiClient.h>
#include "../OpenFrameConfig.h"
#if OF_ENABLE_TLS
#include <WiFiClientSecure.h>
#endif
#include <functional>
#include <map>
#include <vector>
#include "../core/Logger.h"
#include "../core/EventBus.h"
#include "../core/ConfigManager.h"
#include "../OpenFrameConfig.h"

// ── MqttManager ───────────────────────────────────────────────────────────────
//
// Handles:
//  • Connection to the configured MQTT broker
//  • publish() / subscribe() helpers with base-topic prefix
//  • Automatic reconnection with exponential back-off
//  • Routes incoming messages to the EventBus as MqttMessageReceived

class MqttManager {
public:
    using MessageCallback = std::function<void(const String& topic, const String& payload)>;

    static MqttManager& instance();

    // Call once in setup() after WiFi is ready
    void begin();

    // Call every loop()
    void loop();

    // Publish payload to <baseTopic>/<subtopic>
    bool publish(const String& subtopic, const String& payload, bool retained = false);

    // Publish to a full (absolute) topic
    bool publishRaw(const String& topic, const String& payload, bool retained = false);

    // Subscribe to <baseTopic>/<subtopic>
    void subscribe(const String& subtopic, MessageCallback cb);

    // Subscribe to a full (absolute) topic
    void subscribeRaw(const String& topic, MessageCallback cb);

    bool isConnected();

    // Human-readable last connection error ("" while connected / never failed) —
    // surfaced in /api/status so the UI can explain why MQTT is down.
    String lastError() const { return _lastError; }

    String baseTopic() const;

    // Fleet topic contract: <baseTopic>/<deviceId>/<subtopic>. This namespaces
    // every node under its stable id so a CMS can address devices individually
    // (e.g. .../status, /telemetry, /cmd, /cmd/result, retained /online).
    String deviceTopic(const String& subtopic) const;

    // Publish to <baseTopic>/<deviceId>/<subtopic>
    bool publishDevice(const String& subtopic, const String& payload, bool retained = false);

private:
    MqttManager() = default;

    void connect();
    void configureTransport();   // pick plain vs TLS client + load CA / insecure
    void onMessage(const char* topic, const uint8_t* payload, unsigned int length);
    void resubscribeAll();

    WiFiClient        _wifiClient;
#if OF_ENABLE_TLS
  #if defined(ESP32)
    WiFiClientSecure  _secureClient;
  #elif defined(ESP8266)
    BearSSL::WiFiClientSecure _secureClient;
    BearSSL::X509List _caList;   // must outlive _secureClient
  #endif
#endif
    String           _caCert;    // PEM, loaded from OF_MQTT_CA_PATH
    bool             _tlsActive = false;
    PubSubClient     _client;

    // Registered subscriptions
    struct Subscription {
        String          topic;   // full absolute topic
        MessageCallback cb;
    };
    std::vector<Subscription> _subscriptions;

    bool     _enabled       = false;
    uint32_t _lastAttempt   = 0;
    uint32_t _backoffMs     = 2000;
    String   _lastError;

    static constexpr uint32_t MAX_BACKOFF_MS = 60000;
    static constexpr const char* TAG         = "MQTT";
};
