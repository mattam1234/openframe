#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <map>
#include <vector>
#include <functional>
#include "../core/Logger.h"
#include "../core/EventBus.h"
#include "../core/ConfigManager.h"
#include "MqttManager.h"
#include "../OpenFrameConfig.h"

// ── Entity types supported by Home Assistant MQTT Discovery ──────────────────

enum class HaEntityType : uint8_t {
    Sensor,
    BinarySensor,
    Button,
    Switch,
    Select,
    Number,
    Text
};

// ── HaEntity — describes a single HA entity ──────────────────────────────────

struct HaEntity {
    HaEntityType type;
    String       id;          // unique ID within this device
    String       name;        // friendly name
    String       unit;        // for sensors (e.g. "°C")
    String       deviceClass; // optional HA device_class
    float        min  = 0;    // for number entities
    float        max  = 100;  // for number entities
    float        step = 1;    // for number entities
    std::vector<String> options; // for select entities
};

// ── HaManager ─────────────────────────────────────────────────────────────────
//
// Handles:
//  • Registering entities and publishing MQTT Discovery payloads
//  • Receiving HA commands and emitting internal EventBus events
//  • Publishing state updates for registered entities

class HaManager {
public:
    using CommandCallback = std::function<void(const String& entityId, const String& payload)>;

    static HaManager& instance();

    // Call once in setup() after MqttManager is ready
    void begin();

    // Register an entity — must be called before publishDiscovery()
    void registerEntity(const HaEntity& entity);

    // Publish MQTT Discovery payloads for all registered entities
    void publishDiscovery();

    // Publish a state value for an entity
    void publishState(const String& entityId, const String& state);

    // Subscribe to command callbacks for an entity
    void onCommand(const String& entityId, CommandCallback cb);

    bool isEnabled() const;

private:
    HaManager() = default;

    String discoveryTopic(const HaEntity& e) const;
    String stateTopic(const String& entityId) const;
    String commandTopic(const String& entityId) const;
    String entityTypeStr(HaEntityType type) const;

    void buildDiscoveryPayload(const HaEntity& e, JsonDocument& doc) const;
    void handleCommand(const String& topic, const String& payload);

    std::map<String, HaEntity>          _entities;
    std::map<String, String>            _cmdTopicToEntityId;  // command topic → entity ID
    std::map<String, CommandCallback>   _callbacks;

    static constexpr const char* TAG = "HA";
};
