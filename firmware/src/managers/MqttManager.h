#pragma once

#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFiClient.h>
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

    String baseTopic() const;

    // Fleet topic contract: <baseTopic>/<deviceId>/<subtopic>. This namespaces
    // every node under its stable id so a CMS can address devices individually
    // (e.g. .../status, /telemetry, /cmd, /cmd/result, retained /online).
    String deviceTopic(const String& subtopic) const;

    // Publish to <baseTopic>/<deviceId>/<subtopic>
    bool publishDevice(const String& subtopic, const String& payload, bool retained = false);

private:
    MqttManager() : _client(_wifiClient) {}

    void connect();
    void onMessage(const char* topic, const uint8_t* payload, unsigned int length);
    void resubscribeAll();

    WiFiClient  _wifiClient;
    PubSubClient _client;

    // Registered subscriptions
    struct Subscription {
        String          topic;   // full absolute topic
        MessageCallback cb;
    };
    std::vector<Subscription> _subscriptions;

    bool     _enabled       = false;
    uint32_t _lastAttempt   = 0;
    uint32_t _backoffMs     = 2000;

    static constexpr uint32_t MAX_BACKOFF_MS = 60000;
    static constexpr const char* TAG         = "MQTT";
};
