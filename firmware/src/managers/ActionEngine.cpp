#include "ActionEngine.h"

#include <ArduinoJson.h>
#include <time.h>
#include "../core/PlatformCompat.h"
#include "../hardware/DisplayManager.h"
#include "../hardware/OutputManager.h"
#include "../hardware/HidManager.h"
#include "../managers/HaManager.h"
#include "../managers/MqttManager.h"
#include "../managers/NodeLink.h"
#include "../managers/SceneManager.h"
#include "../managers/TimeManager.h"

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
    if (s == "output_control")     return ActionType::OutputControl;
    if (s == "wait_until")         return ActionType::WaitUntil;
    if (s == "scene_restore")      return ActionType::SceneRestore;
    if (s == "ha_service_call")    return ActionType::HaServiceCall;
    if (s == "page_change")        return ActionType::PageChange;
    if (s == "notification")       return ActionType::Notification;
    if (s == "keyboard_shortcut")  return ActionType::KeyboardShortcut;
    if (s == "media_control")      return ActionType::MediaControl;
    if (s == "remote_action")      return ActionType::RemoteAction;
    if (s == "sync_action")        return ActionType::SyncAction;
    if (s == "if")                 return ActionType::If;
    if (s == "else")               return ActionType::Else;
    if (s == "endif")              return ActionType::EndIf;
    if (s == "repeat")             return ActionType::Repeat;
    if (s == "endrepeat")          return ActionType::EndRepeat;
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
        case ActionType::OutputControl:     return "output_control";
        case ActionType::WaitUntil:         return "wait_until";
        case ActionType::SceneRestore:      return "scene_restore";
        case ActionType::HaServiceCall:     return "ha_service_call";
        case ActionType::PageChange:        return "page_change";
        case ActionType::Notification:      return "notification";
        case ActionType::KeyboardShortcut:  return "keyboard_shortcut";
        case ActionType::MediaControl:      return "media_control";
        case ActionType::RemoteAction:      return "remote_action";
        case ActionType::SyncAction:        return "sync_action";
        case ActionType::If:                return "if";
        case ActionType::Else:              return "else";
        case ActionType::EndIf:             return "endif";
        case ActionType::Repeat:            return "repeat";
        case ActionType::EndRepeat:         return "endrepeat";
        default:                            return "delay";
    }
}

// Render a variable's current value as a string for template expansion,
// matching how the API serialises each type.
String varValueToString(const Variable& var) {
    switch (var.type) {
        case VarType::Integer: return String(var.valueInt);
        case VarType::Float:   return String(var.valueFloat);
        case VarType::Boolean: return var.valueBool ? "true" : "false";
        case VarType::String:  return var.valueString;
    }
    return String("");
}

// Expand {{variableId}} tokens in `in` with the live value of each variable.
// Unknown variables expand to an empty string; an unterminated "{{" is emitted
// verbatim. Strings without any "{{" are returned untouched (cheap fast path).
String expandTemplate(const String& in) {
    if (in.indexOf("{{") < 0) return in;

    String out;
    out.reserve(in.length());
    int i = 0;
    const int n = static_cast<int>(in.length());
    while (i < n) {
        const int open = in.indexOf("{{", i);
        if (open < 0) { out += in.substring(i); break; }
        out += in.substring(i, open);
        const int close = in.indexOf("}}", open + 2);
        if (close < 0) { out += in.substring(open); break; }

        String key = in.substring(open + 2, close);
        key.trim();
        const Variable* var = VariableManager::instance().get(key);
        if (var) out += varValueToString(*var);
        i = close + 2;
    }
    return out;
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
        s.nodeId       = item["node_id"] | String("");
        s.outputId     = item["output_id"] | String("");
        s.command      = item["command"] | String("");
        s.on           = item["on"] | false;
        s.red          = item["r"] | 0;
        s.green        = item["g"] | 0;
        s.blue         = item["b"] | 0;
        s.brightnessVal = item["brightness"] | 0;
        s.animation    = item["animation"] | String("");
        s.speed        = item["speed"] | 128;
        s.frequency    = item["frequency"] | 2000;
        s.durationMs   = item["duration_ms"] | 200;
        s.angle        = item["angle"] | 90;
        s.position     = item["position"] | 0;
        s.op           = item["op"] | String("eq");
        s.repeatCount  = item["repeat_count"] | 1;
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
            case ActionType::OutputControl:
                obj["output_id"] = s.outputId;
                obj["command"]   = s.command;
                if (s.command == "digital")    obj["on"] = s.on;
                if (s.command == "rgb" || s.command == "animation") {
                    obj["r"] = s.red; obj["g"] = s.green; obj["b"] = s.blue;
                }
                if (s.command == "brightness") obj["brightness"] = s.brightnessVal;
                if (s.command == "animation") { obj["animation"] = s.animation; obj["speed"] = s.speed; }
                if (s.command == "beep") { obj["frequency"] = s.frequency; obj["duration_ms"] = s.durationMs; }
                if (s.command == "angle") { obj["angle"] = s.angle; }
                if (s.command == "move") { obj["position"] = s.position; }
                break;
            case ActionType::WaitUntil:         obj["variable_id"] = s.variableId; obj["op"] = s.op; obj["value"] = s.value; obj["delay_ms"] = s.delayMs; break;
            case ActionType::SceneRestore:      obj["value"] = s.value; break;
            case ActionType::HaServiceCall:     obj["ha_service"] = s.haService; obj["ha_entity_id"] = s.haEntityId; obj["body"] = s.body; break;
            case ActionType::PageChange:        obj["display_id"] = s.displayId; obj["page_id"] = s.pageId; break;
            case ActionType::Notification:      obj["message"] = s.message; break;
            case ActionType::KeyboardShortcut:  obj["keys"] = s.keysCombo; break;
            case ActionType::MediaControl:      obj["media_command"] = s.mediaCommand; break;
            case ActionType::RemoteAction:      obj["node_id"] = s.nodeId; obj["value"] = s.value; break;
            case ActionType::SyncAction:        obj["value"] = s.value; obj["delay_ms"] = s.delayMs; break;
            case ActionType::If:                obj["variable_id"] = s.variableId; obj["op"] = s.op; obj["value"] = s.value; break;
            case ActionType::Repeat:            obj["repeat_count"] = s.repeatCount; break;
            case ActionType::Else:
            case ActionType::EndIf:
            case ActionType::EndRepeat:         break;  // markers carry no fields
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

// One-line, human-readable summary of a step for dry-run output.
static String describeStep(const ActionStep& s) {
    switch (s.type) {
        case ActionType::Delay:        return "delay " + String(s.delayMs) + "ms";
        case ActionType::HttpRequest:  return "HTTP " + s.method + " " + s.url;
        case ActionType::MqttPublish:  return "MQTT publish → " + s.topic;
        case ActionType::VariableSet:  return "set " + s.variableId + " = " + s.value;
        case ActionType::VariableIncrement: return "increment " + s.variableId + " by " + String(s.increment);
        case ActionType::VariableToggle:    return "toggle " + s.variableId;
        case ActionType::OutputControl: {
            String d = "output " + s.outputId + " → " + s.command;
            if (s.command == "digital") d += s.on ? " on" : " off";
            else if (s.command == "brightness") d += " " + String(s.brightnessVal);
            else if (s.command == "angle") d += " " + String(s.angle) + "°";
            else if (s.command == "move") d += " " + String(s.position);
            else if (s.command == "rgb") d += " (" + String(s.red) + "," + String(s.green) + "," + String(s.blue) + ")";
            else if (s.command == "animation") d += " " + s.animation;
            return d;
        }
        case ActionType::WaitUntil:    return "wait until " + s.variableId + " " + s.op + " " + s.value + " (timeout " + String(s.delayMs ? s.delayMs : 30000) + "ms)";
        case ActionType::SceneRestore: return "restore scene '" + s.value + "'";
        case ActionType::HaServiceCall: return "HA call " + s.haService + " → " + s.haEntityId;
        case ActionType::PageChange:   return "display " + s.displayId + " → page " + s.pageId;
        case ActionType::Notification: return "notify: " + s.message;
        case ActionType::KeyboardShortcut: return "keyboard " + s.keysCombo;
        case ActionType::MediaControl: return "media " + s.mediaCommand;
        case ActionType::RemoteAction: return "remote action " + s.value + " → node " + s.nodeId;
        case ActionType::SyncAction:   return "sync action " + s.value;
        default:                       return String(actionTypeToString(s.type));
    }
}

bool ActionRunner::run(const ActionConfig& action, String& error, bool dryRun,
                       std::vector<String>* log) {
    if (!evaluateConditions(action.conditions)) {
        if (dryRun && log) log->push_back("(skipped — action conditions not met)");
        return true;
    }

    const auto& steps = action.steps;
    const int n = static_cast<int>(steps.size());

    // ── Match control-flow brackets up front ──────────────────────────────────
    // jumpIfFalse[ifIdx]   : pc to resume at when the If condition is false
    // jumpElseEnd[elseIdx] : pc of the matching EndIf (Else jumps here after the
    //                        true branch ran)
    // endOfRepeat[repIdx]  : pc of the matching EndRepeat
    std::vector<int> jumpIfFalse(n, -1), jumpElseEnd(n, -1), endOfRepeat(n, -1), elseForIf(n, -1);
    std::vector<int> ifStack, repStack;
    for (int i = 0; i < n; ++i) {
        switch (steps[i].type) {
            case ActionType::If:    ifStack.push_back(i); break;
            case ActionType::Else:  if (!ifStack.empty()) elseForIf[ifStack.back()] = i; break;
            case ActionType::EndIf:
                if (!ifStack.empty()) {
                    const int ifIdx = ifStack.back(); ifStack.pop_back();
                    if (elseForIf[ifIdx] >= 0) {
                        jumpIfFalse[ifIdx]            = elseForIf[ifIdx] + 1;  // run else body
                        jumpElseEnd[elseForIf[ifIdx]] = i;                     // else → endif
                    } else {
                        jumpIfFalse[ifIdx] = i;  // no else → skip to endif
                    }
                }
                break;
            case ActionType::Repeat:    repStack.push_back(i); break;
            case ActionType::EndRepeat:
                if (!repStack.empty()) { endOfRepeat[repStack.back()] = i; repStack.pop_back(); }
                break;
            default: break;
        }
    }

    // An unbalanced If/Repeat leaves its jump target at -1. Executing anyway
    // silently runs the body of a false If (the >= 0 guard at the If case fails
    // open), so reject malformed bracketing up front instead.
    if (!ifStack.empty() || !repStack.empty()) {
        error = "Unmatched If/Repeat bracket in action";
        return false;
    }

    // ── Execute ───────────────────────────────────────────────────────────────
    struct LoopFrame { int startIdx; int endIdx; uint16_t remaining; };
    std::vector<LoopFrame> loops;
    constexpr int GUARD_MAX = 100000;  // backstop against malformed nesting
    int guard = 0;

    for (int pc = 0; pc < n; ++pc) {
        if (++guard > GUARD_MAX) { error = "Action step guard tripped (check loops/brackets)"; return false; }
        const ActionStep& s = steps[pc];

        switch (s.type) {
            case ActionType::If: {
                Condition c; c.variableId = s.variableId; c.op = conditionOpFromString(s.op); c.value = s.value;
                if (!evaluateCondition(c) && jumpIfFalse[pc] >= 0) {
                    pc = jumpIfFalse[pc] - 1;  // -1: the for-loop ++ lands us on the target
                }
                break;  // condition true → fall into the body
            }
            case ActionType::Else:
                // Hit only after the true branch ran → jump past the else body.
                if (jumpElseEnd[pc] >= 0) pc = jumpElseEnd[pc];
                break;
            case ActionType::EndIf:
                break;  // no-op marker
            case ActionType::Repeat: {
                const int end = endOfRepeat[pc];
                if (s.repeatCount == 0 && end >= 0) {
                    pc = end;  // zero iterations → skip the body
                } else if (end >= 0) {
                    loops.push_back({ pc, end, s.repeatCount });
                }
                break;
            }
            case ActionType::EndRepeat:
                if (!loops.empty() && loops.back().endIdx == pc) {
                    if (--loops.back().remaining > 0) {
                        pc = loops.back().startIdx;  // for-loop ++ re-enters the body
                    } else {
                        loops.pop_back();
                    }
                }
                break;
            default: {
                if (dryRun) {
                    if (log) log->push_back(describeStep(s));
                    break;  // simulate: record, don't execute
                }
                auto it = _executors.find(s.type);
                if (it == _executors.end()) { error = "No executor for action type"; return false; }
                if (!it->second(s, error)) return false;
                break;
            }
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

        // Run any action whose trigger is bound to this digital-input event.
        EventBus::instance().subscribe(EventType::InputDigitalChanged, [](const Event& event) {
            ActionEngine::instance().onInputEvent(event.sourceId, event.payload);
        });

        // Actions requested by another node over NodeLink:
        //   "trigger=<id>"          → run now
        //   "trigger@<epoch>=<id>"  → run at a shared cluster-clock epoch (sync effect)
        NodeLinkManager::instance().onMessage([](const NodeMessage& msg) {
            if (msg.type != NodeMsgType::Cmd) return;
            if (msg.payload.startsWith("trigger@")) {
                const int eq = msg.payload.indexOf('=');
                if (eq < 0) return;
                const uint32_t epoch = (uint32_t)msg.payload.substring(8, eq).toInt();
                ActionEngine::instance().scheduleAction(epoch, msg.payload.substring(eq + 1));
            } else if (msg.payload.startsWith("trigger=")) {
                const String actionId = msg.payload.substring(8);
                String error;
                if (ActionEngine::instance().triggerAction(actionId, error)) {
                    LOG_I(TAG, "Remote-triggered '" + actionId + "' from " + msg.srcId);
                } else {
                    LOG_W(TAG, "Remote trigger '" + actionId + "' from " + msg.srcId + " failed: " + error);
                }
            }
        });
    }

    LOG_I(TAG, "Initialised (" + String(_actions.size()) + " actions)");
    return true;
}

void ActionEngine::loop() {
    evaluateSchedules();
    evaluateDebounced();

    if (_scheduled.empty()) return;
    // Fire scheduled (synchronized) triggers once the cluster clock reaches them.
    // If time is unknown (epoch 0), fire best-effort immediately.
    const uint32_t now = TimeManager::instance().epoch();
    for (size_t i = 0; i < _scheduled.size();) {
        if (now == 0 || now >= _scheduled[i].epoch) {
            const String id = _scheduled[i].actionId;
            _scheduled.erase(_scheduled.begin() + i);
            String error;
            triggerAction(id, error);
        } else {
            ++i;
        }
    }
}

void ActionEngine::evaluateSchedules() {
    const uint32_t nowMs = millis();
    const uint32_t epoch = TimeManager::instance().epoch();
    const bool haveClock = TimeManager::instance().isValid();

    for (auto& a : _actions) {
        if (!a.enabled || a.trigger.source != "schedule") continue;

        if (a.trigger.scheduleMode == "interval") {
            const uint32_t intervalMs = a.trigger.intervalSec * 1000UL;
            if (intervalMs == 0) continue;
            // Arm on first sight so we don't fire immediately at boot.
            if (!a.scheduleArmed) {
                a.scheduleArmed = true;
                a.lastScheduleMs = nowMs;
                continue;
            }
            if (nowMs - a.lastScheduleMs >= intervalMs) {
                a.lastScheduleMs = nowMs;
                triggerAction(a.id);
            }
        } else if (a.trigger.scheduleMode == "daily") {
            if (a.trigger.dailySeconds < 0 || !haveClock) continue;
            // Convert the UTC epoch to local time (honours the configured TZ + DST)
            // so "daily at 08:00" means 08:00 wall-clock, not 08:00 UTC.
            time_t t = static_cast<time_t>(epoch);
            struct tm lt;
            localtime_r(&t, &lt);
            const uint32_t secOfDay = lt.tm_hour * 3600UL + lt.tm_min * 60UL + lt.tm_sec;
            // A unique, monotonic-per-local-day key (only compared for inequality).
            // tm_yday resets at year end but tm_year increments, so the key still
            // advances. (tm_gmtoff isn't available in this newlib build.)
            const int32_t  day      = lt.tm_year * 1000 + lt.tm_yday;
            // Arm so a time that already passed today isn't "caught up" on boot.
            if (!a.scheduleArmed) {
                a.scheduleArmed = true;
                a.lastDailyDay  = (secOfDay >= static_cast<uint32_t>(a.trigger.dailySeconds)) ? day : day - 1;
            }
            if (day != a.lastDailyDay && secOfDay >= static_cast<uint32_t>(a.trigger.dailySeconds)) {
                a.lastDailyDay = day;
                triggerAction(a.id);
            }
        }
    }
}

void ActionEngine::scheduleAction(uint32_t epoch, const String& actionId) {
    if (!actionId.length()) return;
    _scheduled.push_back({ epoch, actionId });
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

bool ActionEngine::removeAction(const String& actionId) {
    for (auto it = _actions.begin(); it != _actions.end(); ++it) {
        if (it->id == actionId) {
            _actions.erase(it);
            return true;
        }
    }
    return false;
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

bool ActionEngine::simulateAction(const String& actionId, std::vector<String>& log, String& error) {
    for (const auto& action : _actions) {
        if (action.id == actionId) {
            return ActionRunner::instance().run(action, error, /*dryRun=*/true, &log);
        }
    }
    error = "Action not found: " + actionId;
    return false;
}

bool ActionEngine::saveActions() const {
    JsonDocument doc;
    auto arr = doc["actions"].to<JsonArray>();
    for (const auto& action : _actions) {
        auto obj = arr.add<JsonObject>();
        obj["id"]      = action.id;
        obj["name"]    = action.name;
        obj["enabled"] = action.enabled;
        auto trig = obj["trigger"].to<JsonObject>();
        trig["source"]        = action.trigger.source;
        trig["input_id"]      = action.trigger.inputId;
        trig["event"]         = action.trigger.event;
        trig["schedule_mode"] = action.trigger.scheduleMode;
        trig["interval_sec"]  = action.trigger.intervalSec;
        trig["daily_seconds"] = action.trigger.dailySeconds;
        trig["debounce_ms"]   = action.trigger.debounceMs;
        trig["cooldown_ms"]   = action.trigger.cooldownMs;
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

        if (item["trigger"].is<JsonObjectConst>()) {
            JsonObjectConst t = item["trigger"].as<JsonObjectConst>();
            action.trigger.source       = t["source"]        | String("");
            action.trigger.inputId      = t["input_id"]      | String("");
            action.trigger.event        = t["event"]         | String("");
            action.trigger.scheduleMode = t["schedule_mode"] | String("");
            action.trigger.intervalSec  = t["interval_sec"]  | 0;
            action.trigger.dailySeconds = t["daily_seconds"] | -1;
            action.trigger.debounceMs   = t["debounce_ms"]   | 0;
            action.trigger.cooldownMs   = t["cooldown_ms"]   | 0;
        }

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
        // Chunk the wait and feed the watchdog: on ESP32 delay()/vTaskDelay()
        // yields the scheduler but does NOT reset the task WDT (armed at 60s in
        // HealthMonitor), so a delay_ms >= 60000 would hard-reboot mid-step.
        constexpr uint32_t CHUNK_MS = 5000;
        uint32_t remaining = step.delayMs;
        while (remaining > 0) {
            const uint32_t chunk = remaining < CHUNK_MS ? remaining : CHUNK_MS;
            delay(chunk);
            of_watchdog_feed();
            remaining -= chunk;
        }
        return true;
    });

    // Block until (variableId op value) holds, or the timeout elapses. delayMs is
    // the timeout (0 → a 30s safety cap so a never-true condition can't hang the
    // device forever). A timeout fails the step, aborting the rest of the action.
    runner.registerExecutor(ActionType::WaitUntil, [](const ActionStep& step, String& error) -> bool {
        Condition c;
        c.variableId = step.variableId;
        c.op         = conditionOpFromString(step.op);
        c.value      = expandTemplate(step.value);
        const uint32_t timeout = step.delayMs > 0 ? step.delayMs : 30000;
        const uint32_t start   = millis();
        while (!ActionRunner::instance().testCondition(c)) {
            if (millis() - start >= timeout) {
                error = "wait_until timed out after " + String(timeout) + "ms";
                return false;
            }
            delay(10);
            of_watchdog_feed();  // ESP32 delay() doesn't reset the task WDT — a
                                 // user timeout >= 60s would otherwise reboot
        }
        return true;
    });

    runner.registerExecutor(ActionType::SceneRestore, [](const ActionStep& step, String& error) -> bool {
        if (!step.value.length()) { error = "scene_restore needs a scene name"; return false; }
        return SceneManager::instance().restore(expandTemplate(step.value), error);
    });

    runner.registerExecutor(ActionType::MqttPublish, [](const ActionStep& step, String& error) -> bool {
        if (!step.topic.length()) {
            error = "MQTT topic required";
            return false;
        }
        MqttManager::instance().publish(expandTemplate(step.topic), expandTemplate(step.body));
        return true;
    });

    // Cross-node automation: ask another node (by deviceId) to run one of its
    // actions, over NodeLink/ESP-NOW. `value` carries the remote action id.
    runner.registerExecutor(ActionType::RemoteAction, [](const ActionStep& step, String& error) -> bool {
        if (!step.nodeId.length() || !step.value.length()) {
            error = "RemoteAction needs node_id and value (remote action id)";
            return false;
        }
        if (!NodeLinkManager::instance().enabled()) {
            error = "NodeLink disabled";
            return false;
        }
        if (!NodeLinkManager::instance().send(step.nodeId, NodeMsgType::Cmd, "trigger=" + step.value)) {
            error = "send to " + step.nodeId + " failed";
            return false;
        }
        return true;
    });

    // Synchronized effect: tell every node to run an action at a shared future
    // cluster-clock instant, so e.g. LED animations fire in unison. `value` is the
    // action id; `delay_ms` is the lead time (default 2 s).
    runner.registerExecutor(ActionType::SyncAction, [](const ActionStep& step, String& error) -> bool {
        if (!step.value.length()) {
            error = "SyncAction needs value (action id)";
            return false;
        }
        if (!NodeLinkManager::instance().enabled()) {
            error = "NodeLink disabled";
            return false;
        }
        const uint32_t leadMs = step.delayMs ? step.delayMs : 2000;
        const uint32_t fireEpoch = TimeManager::instance().epoch() + leadMs / 1000;
        NodeLinkManager::instance().broadcast(NodeMsgType::Cmd, "trigger@" + String(fireEpoch) + "=" + step.value);
        ActionEngine::instance().scheduleAction(fireEpoch, step.value);
        return true;
    });

    runner.registerExecutor(ActionType::VariableSet, [](const ActionStep& step, String& error) -> bool {
        const Variable* var = VariableManager::instance().get(step.variableId);
        if (!var) {
            error = "Unknown variable: " + step.variableId;
            return false;
        }
        const String value = expandTemplate(step.value);
        switch (var->type) {
            case VarType::Integer: VariableManager::instance().setInt(step.variableId, value.toInt()); break;
            case VarType::Float:   VariableManager::instance().setFloat(step.variableId, value.toFloat()); break;
            case VarType::Boolean: VariableManager::instance().setBool(step.variableId, value == "true" || value == "1"); break;
            case VarType::String:  VariableManager::instance().setString(step.variableId, value); break;
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

    runner.registerExecutor(ActionType::OutputControl, [](const ActionStep& step, String& error) -> bool {
        if (!step.outputId.length()) {
            error = "OutputControl needs an output_id";
            return false;
        }
        auto& out = OutputManager::instance();
        bool ok = false;
        if (step.command == "digital") {
            ok = out.setDigital(step.outputId, step.on);
        } else if (step.command == "rgb") {
            ok = out.setRgb(step.outputId, step.red, step.green, step.blue);
        } else if (step.command == "brightness") {
            ok = out.setBrightness(step.outputId, step.brightnessVal);
        } else if (step.command == "animation") {
            // Optional colour rides along, mirroring the /api/outputs/control endpoint.
            if (step.red || step.green || step.blue) {
                out.setRgb(step.outputId, step.red, step.green, step.blue);
            }
            ok = out.setAnimation(step.outputId, ledAnimationFromString(step.animation), step.speed);
        } else if (step.command == "beep") {
            ok = out.beep(step.outputId, step.frequency, step.durationMs);
        } else if (step.command == "angle") {
            ok = out.setAngle(step.outputId, step.angle);
        } else if (step.command == "move") {
            ok = out.setPosition(step.outputId, step.position);  // absolute step target
        } else {
            error = "Unknown output command: " + step.command;
            return false;
        }
        if (!ok) error = "Output '" + step.outputId + "' rejected command '" + step.command + "'";
        return ok;
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
            deserializeJson(bodyDoc, expandTemplate(step.body));
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
        doc["message"] = expandTemplate(step.message);
        String payload;
        serializeJson(doc, payload);
        EventBus::instance().publish(EventType::NotificationPosted, "action", payload);
        return true;
    });

    runner.registerExecutor(ActionType::KeyboardShortcut, [](const ActionStep& step, String& error) -> bool {
        if (!step.keysCombo.length()) { error = "Empty key combo"; return false; }
        return HidManager::instance().sendShortcut(step.keysCombo, error);
    });

    runner.registerExecutor(ActionType::MediaControl, [](const ActionStep& step, String& error) -> bool {
        if (!step.mediaCommand.length()) { error = "Empty media command"; return false; }
        return HidManager::instance().sendMedia(step.mediaCommand, error);
    });

    runner.registerExecutor(ActionType::HttpRequest, [](const ActionStep& step, String& error) -> bool {
        if (!step.url.length()) {
            error = "URL required";
            return false;
        }
        // Interpolate {{variable}} tokens so this doubles as a templated webhook —
        // e.g. POST {"temp": {{sensor_temp}}} to a Slack/IFTTT/custom endpoint.
        const String url  = expandTemplate(step.url);
        const String body = expandTemplate(step.body);
        HTTPClient http;
        // Portable form: the URL-only begin() overload is an obsolete hard error
        // on the ESP8266 core. begin(WiFiClient, url) works on both platforms.
        WiFiClient netClient;
        http.begin(netClient, url);
        http.addHeader("Content-Type", "application/json");
        int code = 0;
        if (step.method == "POST") {
            code = http.POST(body);
        } else if (step.method == "PUT") {
            code = http.PUT(body);
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

void ActionEngine::onInputEvent(const String& inputId, const String& payload) {
    JsonDocument doc;
    if (deserializeJson(doc, payload) != DeserializationError::Ok) return;
    const String event = doc["event"] | String("");
    if (!event.length()) return;

    const uint32_t nowMs = millis();

    // Run every enabled action bound to this (input, event) pair, honouring its
    // debounce/cooldown modifiers.
    for (auto& action : _actions) {
        if (!action.enabled) continue;
        if (action.trigger.source != "input") continue;
        if (action.trigger.inputId != inputId) continue;
        if (action.trigger.event != event) continue;

        if (action.trigger.debounceMs > 0) {
            // Defer (and coalesce) — the actual fire happens in evaluateDebounced()
            // once the input has been quiet for the debounce window.
            action.debounceFireMs = nowMs + action.trigger.debounceMs;
            if (action.debounceFireMs == 0) action.debounceFireMs = 1;  // avoid the "none" sentinel
            continue;
        }
        fireFromTrigger(action);
    }
}

bool ActionEngine::fireFromTrigger(ActionConfig& action) {
    const uint32_t nowMs = millis();
    if (action.trigger.cooldownMs > 0 && action.everFired &&
        (nowMs - action.lastFiredMs) < action.trigger.cooldownMs) {
        return false;  // still cooling down — drop this trigger
    }
    action.lastFiredMs = nowMs;
    action.everFired   = true;
    triggerAction(action.id);
    return true;
}

void ActionEngine::evaluateDebounced() {
    const uint32_t nowMs = millis();
    for (auto& action : _actions) {
        if (action.debounceFireMs == 0) continue;
        // Unsigned wrap-safe "now >= due": the window is small relative to 2^32.
        if ((int32_t)(nowMs - action.debounceFireMs) < 0) continue;
        action.debounceFireMs = 0;
        if (!action.enabled) continue;
        fireFromTrigger(action);
    }
}
