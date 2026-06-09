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
    HaServiceCall,
    PageChange,
    Notification,
    KeyboardShortcut,
    MediaControl,
    RemoteAction,   // trigger an action on another node over NodeLink (ESP-NOW)
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
};

struct ActionConfig {
    String                  id;
    String                  name;
    bool                    enabled    = true;
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
    bool triggerAction(const String& actionId, String& error);
    bool triggerAction(const String& actionId);

    const std::vector<ActionConfig>& actions() const { return _actions; }
    const std::vector<ActionHistoryEntry>& history() const { return _history; }

    bool saveActions() const;
    bool loadActions();

private:
    ActionEngine() = default;

    void registerBuiltInExecutors();
    void recordHistory(const String& id, const String& name, bool success, const String& error);
    void onEventTriggered(const Event& event);

    std::vector<ActionConfig>       _actions;
    std::vector<ActionHistoryEntry> _history;
    bool                            _subscribed = false;

    static constexpr size_t HISTORY_LIMIT = OF_ACTION_HISTORY_SIZE;
    static constexpr const char* TAG = "ActionEngine";
};
