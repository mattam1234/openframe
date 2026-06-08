#include "ActionEngine.h"

#include <ArduinoJson.h>
#include "../core/PlatformCompat.h"
#include "../hardware/DisplayManager.h"
#include "../managers/HaManager.h"
#include "../managers/MqttManager.h"

namespace {

ConditionOp conditionOpFromString(const String& s) {
    if (s == "ne")  return ConditionOp::Ne;
    if (s == "lt")  return ConditionOp::Lt;
    if (s == "lte") return ConditionOp::Lte;
    if (s == "gt")  return ConditionOp::Gt;
    if (s == "gte") return ConditionOp::Gte;
    return ConditionOp::Eq;
}

ActionType actionTypeFromString(const String& s) {
    if (s == "delay")              return ActionType::Delay;
    if (s == "http_request")       return ActionType::HttpRequest;
    if (s == "mqtt_publish")       return ActionType::MqttPublish;
    if (s == "variable_set")       return ActionType::VariableSet;
    if (s == "variable_increment") return ActionType::VariableIncrement;
    if (s == "variable_toggle")    return ActionType::VariableToggle;
    if (s == "ha_service_call")    return ActionType::HaServiceCall;
    if (s == "page_change")        return ActionType::PageChange;
    if (s == "notification")       return ActionType::Notification;
    if (s == "keyboard_shortcut")  return ActionType::KeyboardShortcut;
    if (s == "media_control")      return ActionType::MediaControl;
    return ActionType::Delay;
}

const char* actionTypeToString(ActionType t) {
    switch (t) {
        case ActionType::Delay:             return "delay";
        case ActionType::HttpRequest:       return "http_request";
        case ActionType::MqttPublish:       return "mqtt_publish";
        case ActionType::VariableSet:       return "variable_set";
        case ActionType::VariableIncrement: return "variable_increment";
        case ActionType::VariableToggle:    return "variable_toggle";
        case ActionType::HaServiceCall:     return "ha_service_call";
        case ActionType::PageChange:        return "page_change";
        case ActionType::Notification:      return "notification";
        case ActionType::KeyboardShortcut:  return "keyboard_shortcut";
        case ActionType::MediaControl:      return "media_control";
        default:                            return "delay";
    }
}

void deserializeConditions(const JsonArrayConst& arr, std::vector<Condition>& out) {
    out.clear();
    for (JsonObjectConst item : arr) {
        Condition c;
        c.variableId = item["variable_id"] | String("");
        c.op         = conditionOpFromString(item["op"] | String("eq"));
        c.value      = item["value"] | String("");
        if (c.variableId.length()) out.push_back(c);
    }
}

void deserializeSteps(const JsonArrayConst& arr, std::vector<ActionStep>& out) {
    out.clear();
    for (JsonObjectConst item : arr) {
        ActionStep s;
        s.type         = actionTypeFromString(item["type"] | String("delay"));
        s.delayMs      = item["delay_ms"] | 0;
        s.url          = item["url"] | String("");
        s.method       = item["method"] | String("GET");
        s.body         = item["body"] | String("");
        s.topic        = item["topic"] | String("");
        s.variableId   = item["variable_id"] | String("");
        s.value        = item["value"] | String("");
        s.increment    = item["increment"] | 1.0f;
        s.haService    = item["ha_service"] | String("");
        s.haEntityId   = item["ha_entity_id"] | String("");
        s.displayId    = item["display_id"] | String("");
        s.pageId       = item["page_id"] | String("");
        s.message      = item["message"] | String("");
        s.keysCombo    = item["keys"] | String("");
        s.mediaCommand = item["media_command"] | String("");
        out.push_back(s);
    }
}

void serializeConditions(const std::vector<Condition>& conds, JsonArray arr) {
    for (const auto& c : conds) {
        auto obj = arr.add<JsonObject>();
        obj["variable_id"] = c.variableId;
        switch (c.op) {
            case ConditionOp::Ne:  obj["op"] = "ne"; break;
            case ConditionOp::Lt:  obj["op"] = "lt"; break;
            case ConditionOp::Lte: obj["op"] = "lte"; break;
            case ConditionOp::Gt:  obj["op"] = "gt"; break;
            case ConditionOp::Gte: obj["op"] = "gte"; break;
            default:               obj["op"] = "eq"; break;
        }
        obj["value"] = c.value;
    }
}

void serializeSteps(const std::vector<ActionStep>& steps, JsonArray arr) {
    for (const auto& s : steps) {
        auto obj = arr.add<JsonObject>();
        obj["type"] = actionTypeToString(s.type);
        switch (s.type) {
            case ActionType::Delay:             obj["delay_ms"] = s.delayMs; break;
            case ActionType::HttpRequest:       obj["url"] = s.url; obj["method"] = s.method; obj["body"] = s.body; break;
            case ActionType::MqttPublish:       obj["topic"] = s.topic; obj["body"] = s.body; break;
            case ActionType::VariableSet:       obj["variable_id"] = s.variableId; obj["value"] = s.value; break;
            case ActionType::VariableIncrement: obj["variable_id"] = s.variableId; obj["increment"] = s.increment; break;
            case ActionType::VariableToggle:    obj["variable_id"] = s.variableId; break;
            case ActionType::HaServiceCall:     obj["ha_service"] = s.haService; obj["ha_entity_id"] = s.haEntityId; obj["body"] = s.body; break;
            case ActionType::PageChange:        obj["display_id"] = s.displayId; obj["page_id"] = s.pageId; break;
            case ActionType::Notification:      obj["message"] = s.message; break;
            case ActionType::KeyboardShortcut:  obj["keys"] = s.keysCombo; break;
            case ActionType::MediaControl:      obj["media_command"] = s.mediaCommand; break;
        }
    }
}

}  // namespace

ActionRunner& ActionRunner::instance() {
    static ActionRunner inst;
    return inst;
}

void ActionRunner::registerExecutor(ActionType type, StepExecutor executor) {
    _executors[type] = std::move(executor);
}

bool ActionRunner::run(const ActionConfig& action, String& error) {
    if (!evaluateConditions(action.conditions)) {
        return true;
    }

    for (const auto& step : action.steps) {
        auto it = _executors.find(step.type);
        if (it == _executors.end()) {
            error = "No executor for action type";
            return false;
        }
        if (!it->second(step, error)) {
            return false;
        }
    }
    return true;
}

bool ActionRunner::evaluateConditions(const std::vector<Condition>& conditions) const {
    for (const auto& cond : conditions) {
        if (!evaluateCondition(cond)) return false;
    }
    return true;
}

bool ActionRunner::evaluateCondition(const Condition& cond) const {
    const Variable* var = VariableManager::instance().get(cond.variableId);
    if (!var) return false;

    float actual = 0.0f;
    const float expected = cond.value.toFloat();

    switch (var->type) {
        case VarType::Integer: actual = static_cast<float>(var->valueInt); break;
        case VarType::Float:   actual = var->valueFloat; break;
        case VarType::Boolean: actual = var->valueBool ? 1.0f : 0.0f; break;
        case VarType::String:
            switch (cond.op) {
                case ConditionOp::Eq: return var->valueString == cond.value;
                case ConditionOp::Ne: return var->valueString != cond.value;
                default: return false;
            }
    }

    switch (cond.op) {
        case ConditionOp::Eq:  return actual == expected;
        case ConditionOp::Ne:  return actual != expected;
        case ConditionOp::Lt:  return actual < expected;
        case ConditionOp::Lte: return actual <= expected;
        case ConditionOp::Gt:  return actual > expected;
        case ConditionOp::Gte: return actual >= expected;
    }
    return false;
}

ActionEngine& ActionEngine::instance() {
    static ActionEngine inst;
    return inst;
}

bool ActionEngine::begin() {
    registerBuiltInExecutors();

    if (!loadActions()) {
        LOG_D(TAG, "No actions config found");
    }

    if (!_subscribed) {
        _subscribed = true;
        EventBus::instance().subscribe(EventType::ActionTriggered, [](const Event& event) {
            ActionEngine::instance().onEventTriggered(event);
        });
    }

    LOG_I(TAG, "Initialised (" + String(_actions.size()) + " actions)");
    return true;
}

void ActionEngine::loop() {
    // ActionEngine is event-driven; all execution is triggered by run() calls.
    // No periodic polling is needed.
}

bool ActionEngine::registerAction(const ActionConfig& action) {
    if (!action.id.length()) return false;
    for (auto& existing : _actions) {
        if (existing.id == action.id) {
            existing = action;
            return true;
        }
    }
    _actions.push_back(action);
    return true;
}

bool ActionEngine::triggerAction(const String& actionId, String& error) {
    for (const auto& action : _actions) {
        if (action.id == actionId) {
            if (!action.enabled) {
                error = "Action disabled";
                recordHistory(actionId, action.name, false, error);
                return false;
            }
            const bool ok = ActionRunner::instance().run(action, error);
            recordHistory(actionId, action.name, ok, error);
            if (ok) {
                EventBus::instance().publish(EventType::ActionTriggered, actionId, "{\"id\":\"" + actionId + "\"}");
            }
            return ok;
        }
    }
    error = "Action not found: " + actionId;
    return false;
}

bool ActionEngine::triggerAction(const String& actionId) {
    String error;
    return triggerAction(actionId, error);
}

bool ActionEngine::saveActions() const {
    JsonDocument doc;
    auto arr = doc["actions"].to<JsonArray>();
    for (const auto& action : _actions) {
        auto obj = arr.add<JsonObject>();
        obj["id"]      = action.id;
        obj["name"]    = action.name;
        obj["enabled"] = action.enabled;
        serializeConditions(action.conditions, obj["conditions"].to<JsonArray>());
        serializeSteps(action.steps, obj["steps"].to<JsonArray>());
    }
    return StorageManager::instance().writeJson(OF_ACTIONS_PATH, doc);
}

bool ActionEngine::loadActions() {
    _actions.clear();
    JsonDocument doc;
    if (!StorageManager::instance().readJson(OF_ACTIONS_PATH, doc)) return false;
    if (!doc["actions"].is<JsonArray>()) return true;

    for (JsonObjectConst item : doc["actions"].as<JsonArrayConst>()) {
        ActionConfig action;
        action.id      = item["id"] | String("");
        action.name    = item["name"] | String("");
        action.enabled = item["enabled"] | true;
        if (!action.id.length()) continue;

        if (item["conditions"].is<JsonArrayConst>()) {
            deserializeConditions(item["conditions"].as<JsonArrayConst>(), action.conditions);
        }
        if (item["steps"].is<JsonArrayConst>()) {
            deserializeSteps(item["steps"].as<JsonArrayConst>(), action.steps);
        }

        _actions.push_back(action);
    }
    return true;
}

void ActionEngine::registerBuiltInExecutors() {
    auto& runner = ActionRunner::instance();

    runner.registerExecutor(ActionType::Delay, [](const ActionStep& step, String&) -> bool {
        if (step.delayMs > 0) delay(step.delayMs);
        return true;
    });

    runner.registerExecutor(ActionType::MqttPublish, [](const ActionStep& step, String& error) -> bool {
        if (!step.topic.length()) {
            error = "MQTT topic required";
            return false;
        }
        MqttManager::instance().publish(step.topic, step.body);
        return true;
    });

    runner.registerExecutor(ActionType::VariableSet, [](const ActionStep& step, String& error) -> bool {
        const Variable* var = VariableManager::instance().get(step.variableId);
        if (!var) {
            error = "Unknown variable: " + step.variableId;
            return false;
        }
        switch (var->type) {
            case VarType::Integer: VariableManager::instance().setInt(step.variableId, step.value.toInt()); break;
            case VarType::Float:   VariableManager::instance().setFloat(step.variableId, step.value.toFloat()); break;
            case VarType::Boolean: VariableManager::instance().setBool(step.variableId, step.value == "true" || step.value == "1"); break;
            case VarType::String:  VariableManager::instance().setString(step.variableId, step.value); break;
        }
        return true;
    });

    runner.registerExecutor(ActionType::VariableIncrement, [](const ActionStep& step, String& error) -> bool {
        const Variable* var = VariableManager::instance().get(step.variableId);
        if (!var) {
            error = "Unknown variable: " + step.variableId;
            return false;
        }
        switch (var->type) {
            case VarType::Integer: VariableManager::instance().setInt(step.variableId, var->valueInt + static_cast<int32_t>(step.increment)); break;
            case VarType::Float:   VariableManager::instance().setFloat(step.variableId, var->valueFloat + step.increment); break;
            default: error = "Increment not supported for this type"; return false;
        }
        return true;
    });

    runner.registerExecutor(ActionType::VariableToggle, [](const ActionStep& step, String& error) -> bool {
        const Variable* var = VariableManager::instance().get(step.variableId);
        if (!var || var->type != VarType::Boolean) {
            error = "Toggle requires a boolean variable";
            return false;
        }
        VariableManager::instance().setBool(step.variableId, !var->valueBool);
        return true;
    });

    runner.registerExecutor(ActionType::HaServiceCall, [](const ActionStep& step, String& error) -> bool {
        if (!step.haService.length()) {
            error = "HA service required";
            return false;
        }
        if (!HaManager::instance().isEnabled()) {
            error = "HA not enabled";
            return false;
        }
        const String topic = "homeassistant/call_service/" + step.haService;
        JsonDocument doc;
        doc["entity_id"] = step.haEntityId;
        if (step.body.length()) {
            JsonDocument bodyDoc;
            deserializeJson(bodyDoc, step.body);
            doc["service_data"].set(bodyDoc);
        }
        String payload;
        serializeJson(doc, payload);
        MqttManager::instance().publish(topic, payload);
        return true;
    });

    runner.registerExecutor(ActionType::PageChange, [](const ActionStep& step, String& error) -> bool {
        if (!DisplayManager::instance().setActivePage(step.displayId, step.pageId)) {
            error = "Page change failed for display " + step.displayId + " page " + step.pageId;
            return false;
        }
        return true;
    });

    runner.registerExecutor(ActionType::Notification, [](const ActionStep& step, String&) -> bool {
        JsonDocument doc;
        doc["message"] = step.message;
        String payload;
        serializeJson(doc, payload);
        EventBus::instance().publish(EventType::NotificationPosted, "action", payload);
        return true;
    });

    runner.registerExecutor(ActionType::KeyboardShortcut, [](const ActionStep& step, String&) -> bool {
        LOG_D("ActionEngine", "KeyboardShortcut stub: " + step.keysCombo);
        return true;
    });

    runner.registerExecutor(ActionType::MediaControl, [](const ActionStep& step, String&) -> bool {
        LOG_D("ActionEngine", "MediaControl stub: " + step.mediaCommand);
        return true;
    });

    runner.registerExecutor(ActionType::HttpRequest, [](const ActionStep& step, String& error) -> bool {
        if (!step.url.length()) {
            error = "URL required";
            return false;
        }
        HTTPClient http;
        // Portable form: the URL-only begin() overload is an obsolete hard error
        // on the ESP8266 core. begin(WiFiClient, url) works on both platforms.
        WiFiClient netClient;
        http.begin(netClient, step.url);
        http.addHeader("Content-Type", "application/json");
        int code = 0;
        if (step.method == "POST") {
            code = http.POST(step.body);
        } else if (step.method == "PUT") {
            code = http.PUT(step.body);
        } else {
            code = http.GET();
        }
        http.end();
        if (code < 200 || code >= 300) {
            error = "HTTP " + String(code);
            return false;
        }
        return true;
    });
}

void ActionEngine::recordHistory(const String& id, const String& name, bool success, const String& error) {
    if (_history.size() >= HISTORY_LIMIT) {
        _history.erase(_history.begin());
    }
    _history.push_back(ActionHistoryEntry{ id, name, millis(), success, error });
}

void ActionEngine::onEventTriggered(const Event& event) {
    JsonDocument doc;
    if (deserializeJson(doc, event.payload) == DeserializationError::Ok) {
        const String actionId = doc["id"] | String("");
        if (actionId.length() && actionId != event.sourceId) {
            triggerAction(actionId);
        }
    }
}
