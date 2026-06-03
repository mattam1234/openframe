#pragma once

#include <Arduino.h>
#include <functional>
#include <map>
#include <vector>

// ── Event types ───────────────────────────────────────────────────────────────

enum class EventType : uint16_t {
    // Input
    InputDigitalChanged = 100,
    InputAnalogChanged,
    OutputStateChanged,

    // Sensors
    SensorValueUpdated = 200,
    SensorError,

    // Display
    DisplayPageChanged = 300,
    DisplayTouchEvent,

    // Variable
    VariableChanged = 400,

    // Connectivity
    WifiConnected = 500,
    WifiDisconnected,
    MqttConnected,
    MqttDisconnected,
    MqttMessageReceived,
    HaConnected,
    HaDisconnected,

    // Actions
    ActionTriggered = 600,
    MacroTriggered,

    // System
    OtaStarted = 700,
    OtaProgress,
    OtaFinished,
    OtaError,
    SystemHealthUpdate,
    NotificationPosted,
};

// ── Event ─────────────────────────────────────────────────────────────────────

struct Event {
    EventType type;
    String    sourceId;   // e.g. input ID, sensor ID
    String    payload;    // JSON string carrying event-specific data
};

// ── EventBus ──────────────────────────────────────────────────────────────────

class EventBus {
public:
    using Handler = std::function<void(const Event&)>;

    static EventBus& instance();

    // Subscribe to a specific event type
    void subscribe(EventType type, Handler handler);

    // Subscribe to all events
    void subscribeAll(Handler handler);

    // Publish an event — notifies all matching handlers
    void publish(const Event& event);

    // Convenience overload
    void publish(EventType type, const String& sourceId = "", const String& payload = "");

private:
    EventBus() = default;

    std::map<EventType, std::vector<Handler>> _handlers;
    std::vector<Handler>                      _wildcardHandlers;
};
