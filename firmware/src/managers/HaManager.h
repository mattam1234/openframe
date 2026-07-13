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
    Text,
    Light
};

// ── HaEntity — describes a single HA entity ──────────────────────────────────

struct HaEntity {
    HaEntityType type;
    String       id;            // unique ID within this device
    String       name;          // friendly name
    String       unit;          // for sensors (e.g. "°C")
    String       deviceClass;   // optional HA device_class
    String       stateClass;    // optional HA state_class (e.g. "measurement") — enables long-term statistics
    String       entityCategory;// optional HA entity_category ("diagnostic" / "config")
    float        min  = 0;      // for number entities
    float        max  = 100;    // for number entities
    float        step = 1;      // for number entities
    std::vector<String> options; // for select entities
    // Light entities: extra capabilities (brightness slider / RGB colour).
    bool         brightness = false;
    bool         rgb        = false;
    // Multi-LED WS2812 strip: expose its animations to HA as an effect_list (F2).
    bool         effects    = false;
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

    // Call every loop() — periodically re-publishes all entity states while MQTT
    // is connected so Home Assistant recovers current values after a broker/HA
    // restart (state topics are not retained). No-op when HA is disabled.
    void loop();

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
    void publishLightAttributes(const String& id, JsonObjectConst o);
    void handleCommand(const String& topic, const String& payload);

    // Auto-build HA entities from the device's configured sensors, outputs and
    // digital inputs, and publish their current state. Built once, lazily, on the
    // first MQTT connection (by which point all hardware managers have loaded).
    void buildEntities();
    void publishAllStates();
    // Re-assert the last-known state of every entity on the periodic cadence.
    void republishStates();
    void onSensorEvent(const String& payload);
    void onOutputEvent(const String& payload);
    void onInputEvent(const String& sourceId, const String& payload);

    // Expose display Button/Toggle widgets as HA button/switch entities. Buttons
    // fire the widget's action; toggles drive the bound variable (2-way).
    void buildDisplayWidgetEntities();
    // A variable changed → if a display-toggle switch is bound to it, push state.
    void onVariableChanged(const String& variableId);

    // Device-level diagnostic + control entities (RSSI, uptime, heap, IP, restart).
    void buildDeviceEntities();
    void updateDiagnostics();
    // Clear retained discovery configs for entities removed since the last boot,
    // then persist the current set. Keeps HA free of stale "unavailable" entities.
    void pruneStaleDiscovery();
    // Handle HA's birth message (<discoveryPrefix>/status == "online"): re-announce
    // discovery + state so entities recover immediately after a Home Assistant restart.
    void onHaStatus(const String& payload);

    // Display-toggle switch entities: variableId → {switch entity id, on/off
    // midpoint threshold}, so a variable change pushes the switch's HA state.
    struct HaToggleBinding { String entityId; float threshold; };
    std::map<String, HaToggleBinding>   _toggleByVar;

    std::map<String, HaEntity>          _entities;
    std::map<String, String>            _cmdTopicToEntityId;  // command topic → entity ID
    std::map<String, CommandCallback>   _callbacks;
    std::map<String, String>            _lastState;           // entity ID → last published state
    bool                                _entitiesBuilt = false;

    // Periodic state-republish cadence (mirrors TelemetryManager's heartbeat).
    bool     _wasConnected        = false;
    uint32_t _lastStatePublishMs  = 0;
    static constexpr uint32_t STATE_PUBLISH_INTERVAL_MS = 30000;  // 30 s

    static constexpr const char* TAG = "HA";
};
