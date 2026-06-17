#include "MacroManager.h"

#include <ArduinoJson.h>
#include "ActionEngine.h"
#include "VariableManager.h"

MacroManager& MacroManager::instance() {
    static MacroManager inst;
    return inst;
}

bool MacroManager::begin() {
    if (!loadMacros()) {
        LOG_D(TAG, "No macros config found");
    }

    if (!_subscribed) {
        _subscribed = true;
        EventBus::instance().subscribe(EventType::MacroTriggered, [](const Event& event) {
            MacroManager::instance().onMacroTriggered(event);
        });
    }

    LOG_I(TAG, "Initialised (" + String(_macros.size()) + " macros)");
    return true;
}

void MacroManager::loop() {
    // MacroManager is event-driven; macros are executed synchronously via runMacro().
    // No periodic polling is needed.
}

bool MacroManager::registerMacro(const MacroConfig& macro) {
    if (!macro.id.length()) return false;
    for (auto& existing : _macros) {
        if (existing.id == macro.id) {
            existing = macro;
            return true;
        }
    }
    _macros.push_back(macro);
    return true;
}

bool MacroManager::removeMacro(const String& macroId) {
    for (auto it = _macros.begin(); it != _macros.end(); ++it) {
        if (it->id == macroId) {
            _macros.erase(it);
            return true;
        }
    }
    return false;
}

bool MacroManager::triggerMacro(const String& macroId, String& error) {
    for (const auto& macro : _macros) {
        if (macro.id != macroId) continue;
        if (!macro.enabled) {
            error = "Macro disabled";
            return false;
        }

        EventBus::instance().publish(EventType::MacroTriggered, macroId, "{\"id\":\"" + macroId + "\"}");

        // Apply parameters (#43) before the actions run, converting the string
        // value to each variable's declared type. Unknown variables are skipped.
        for (const auto& p : macro.params) {
            const Variable* v = VariableManager::instance().get(p.variableId);
            if (!v) continue;
            switch (v->type) {
                case VarType::Integer: VariableManager::instance().setInt(p.variableId, p.value.toInt()); break;
                case VarType::Float:   VariableManager::instance().setFloat(p.variableId, p.value.toFloat()); break;
                case VarType::Boolean: VariableManager::instance().setBool(p.variableId, p.value == "true" || p.value == "1"); break;
                case VarType::String:  VariableManager::instance().setString(p.variableId, p.value); break;
            }
        }

        for (const auto& actionId : macro.actionIds) {
            String actionError;
            if (!ActionEngine::instance().triggerAction(actionId, actionError)) {
                LOG_W(TAG, "Macro '" + macroId + "' step failed: " + actionError);
            }
        }
        return true;
    }
    error = "Macro not found: " + macroId;
    return false;
}

bool MacroManager::triggerMacro(const String& macroId) {
    String error;
    return triggerMacro(macroId, error);
}

bool MacroManager::saveMacros() const {
    JsonDocument doc;
    auto arr = doc["macros"].to<JsonArray>();
    for (const auto& macro : _macros) {
        auto obj = arr.add<JsonObject>();
        obj["id"]      = macro.id;
        obj["name"]    = macro.name;
        obj["enabled"] = macro.enabled;
        auto acts = obj["action_ids"].to<JsonArray>();
        for (const auto& id : macro.actionIds) {
            acts.add(id);
        }
        auto params = obj["params"].to<JsonArray>();
        for (const auto& p : macro.params) {
            auto po = params.add<JsonObject>();
            po["variable_id"] = p.variableId;
            po["value"]       = p.value;
        }
    }
    return StorageManager::instance().writeJson(OF_MACROS_PATH, doc);
}

bool MacroManager::loadMacros() {
    _macros.clear();
    JsonDocument doc;
    if (!StorageManager::instance().readJson(OF_MACROS_PATH, doc)) return false;
    if (!doc["macros"].is<JsonArray>()) return true;

    for (JsonObjectConst item : doc["macros"].as<JsonArrayConst>()) {
        MacroConfig macro;
        macro.id      = item["id"] | String("");
        macro.name    = item["name"] | String("");
        macro.enabled = item["enabled"] | true;
        if (!macro.id.length()) continue;

        if (item["action_ids"].is<JsonArrayConst>()) {
            for (JsonVariantConst v : item["action_ids"].as<JsonArrayConst>()) {
                const String id = v | String("");
                if (id.length()) macro.actionIds.push_back(id);
            }
        }
        if (item["params"].is<JsonArrayConst>()) {
            for (JsonObjectConst p : item["params"].as<JsonArrayConst>()) {
                MacroParam mp;
                mp.variableId = p["variable_id"] | String("");
                mp.value      = p["value"] | String("");
                if (mp.variableId.length()) macro.params.push_back(mp);
            }
        }
        _macros.push_back(macro);
    }
    return true;
}

void MacroManager::onMacroTriggered(const Event& event) {
    JsonDocument doc;
    if (deserializeJson(doc, event.payload) == DeserializationError::Ok) {
        const String macroId = doc["id"] | String("");
        if (macroId.length() && macroId != event.sourceId) {
            triggerMacro(macroId);
        }
    }
}
