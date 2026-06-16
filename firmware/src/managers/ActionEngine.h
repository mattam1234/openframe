#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <functional>
#include <map>
#include <memory>
#include <vector>
#include "../core/EventBus.h"
#include "../core/Logger.h"
#include "../core/StorageManager.h"
#include "../managers/VariableManager.h"
#include "../OpenFrameConfig.h"

enum class ConditionOp : uint8_t {
    Eq, Ne, Lt, Lte, Gt, Gte
};

struct Condition {
    String      variableId;
    ConditionOp op    = ConditionOp::Eq;
    String      value;
};

enum class ActionType : uint8_t {
    Delay,
    HttpRequest,
    MqttPublish,
    VariableSet,
    VariableIncrement,
    VariableToggle,
    OutputControl,  // drive an output (LED/relay/RGB/WS2812): on/off, colour, brightness, animation
    HaServiceCall,
    PageChange,
    Notification,
    KeyboardShortcut,
    MediaControl,
    RemoteAction,   // trigger an action on another node over NodeLink (ESP-NOW)
    SyncAction,     // trigger an action on ALL nodes at a shared cluster time
};

struct ActionStep {
    ActionType  type          = ActionType::Delay;
    uint32_t    delayMs       = 0;
    String      url;
    String      method;
    String      body;
    String      topic;
    String      variableId;
    String      value;
    float       increment     = 1.0f;
    String      haService;
    String      haEntityId;
    String      displayId;
    String      pageId;
    String      message;
    String      keysCombo;
    String      mediaCommand;
    String      nodeId;        // RemoteAction: target node's deviceId (value holds the remote action id)

    // OutputControl
    String      outputId;
    String      command;       // "digital" | "rgb" | "brightness" | "animation"
    bool        on            = false;
    uint8_t     red           = 0;
    uint8_t     green         = 0;
    uint8_t     blue          = 0;
    uint8_t     brightnessVal = 0;
    String      animation;     // e.g. "solid", "rainbow", "chase"
    uint8_t     speed         = 128;
    uint16_t    frequency     = 2000;   // buzzer "beep" tone (Hz)
    uint16_t    durationMs    = 200;    // buzzer "beep" duration
};

// Optional trigger that auto-runs an action when a hardware event fires.
struct ActionTrigger {
    String source;   // "" / "manual" → run only on demand; "input" → bound to a digital input
    String inputId;  // for source == "input"
    String event;    // digital-input event: "Press", "Release", "LongPress", "DoublePress"…
};

struct ActionConfig {
    String                  id;
    String                  name;
    bool                    enabled    = true;
    ActionTrigger           trigger;
    std::vector<Condition>  conditions;
    std::vector<ActionStep> steps;
};

struct ActionHistoryEntry {
    String   actionId;
    String   actionName;
    uint32_t timestampMs = 0;
    bool     success     = true;
    String   error;

    ActionHistoryEntry() = default;
    ActionHistoryEntry(const String& id, const String& name, uint32_t timestamp, bool ok, const String& err)
        : actionId(id), actionName(name), timestampMs(timestamp), success(ok), error(err) {}
};

class ActionRunner {
public:
    using StepExecutor = std::function<bool(const ActionStep&, String&)>;

    static ActionRunner& instance();

    void registerExecutor(ActionType type, StepExecutor executor);
    bool run(const ActionConfig& action, String& error);

private:
    ActionRunner() = default;

    bool evaluateConditions(const std::vector<Condition>& conditions) const;
    bool evaluateCondition(const Condition& cond) const;

    std::map<ActionType, StepExecutor> _executors;
};

class ActionEngine {
public:
    static ActionEngine& instance();

    bool begin();
    void loop();

    bool registerAction(const ActionConfig& action);
    bool removeAction(const String& actionId);
    bool triggerAction(const String& actionId, String& error);
    bool triggerAction(const String& actionId);

    // Schedule an action to run at a shared cluster-clock epoch (synchronized effects).
    void scheduleAction(uint32_t epoch, const String& actionId);

    const std::vector<ActionConfig>& actions() const { return _actions; }
    const std::vector<ActionHistoryEntry>& history() const { return _history; }

    bool saveActions() const;
    bool loadActions();

private:
    ActionEngine() = default;

    void registerBuiltInExecutors();
    void recordHistory(const String& id, const String& name, bool success, const String& error);
    void onEventTriggered(const Event& event);
    void onInputEvent(const String& inputId, const String& payload);

    // A cross-node action scheduled to fire at a shared cluster-clock epoch.
    struct ScheduledTrigger { uint32_t epoch; String actionId; };

    std::vector<ActionConfig>       _actions;
    std::vector<ActionHistoryEntry> _history;
    std::vector<ScheduledTrigger>   _scheduled;
    bool                            _subscribed = false;

    static constexpr size_t HISTORY_LIMIT = OF_ACTION_HISTORY_SIZE;
    static constexpr const char* TAG = "ActionEngine";
};
