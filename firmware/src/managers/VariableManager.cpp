#include "VariableManager.h"

VariableManager& VariableManager::instance() {
    static VariableManager inst;
    return inst;
}

bool VariableManager::begin() {
    JsonDocument doc;
    if (StorageManager::instance().readJson(OF_VARIABLES_PATH, doc)) {
        fromJson(doc);
        LOG_I(TAG, "Variables loaded (" + String(_vars.size()) + ")");
    }
    return true;
}

bool VariableManager::save() {
    JsonDocument doc;
    toJson(doc);
    return StorageManager::instance().writeJson(OF_VARIABLES_PATH, doc);
}

void VariableManager::define(const String& id, VarType type, const String& label, bool persistent) {
    if (_vars.count(id)) return;
    Variable v;
    v.id         = id;
    v.type       = type;
    v.label      = label;
    v.persistent = persistent;
    _vars[id]    = v;
}

// ── Getters ───────────────────────────────────────────────────────────────────

int32_t VariableManager::getInt(const String& id) const {
    auto it = _vars.find(id);
    return (it != _vars.end()) ? it->second.valueInt : 0;
}

float VariableManager::getFloat(const String& id) const {
    auto it = _vars.find(id);
    return (it != _vars.end()) ? it->second.valueFloat : 0.0f;
}

bool VariableManager::getBool(const String& id) const {
    auto it = _vars.find(id);
    return (it != _vars.end()) ? it->second.valueBool : false;
}

String VariableManager::getString(const String& id) const {
    auto it = _vars.find(id);
    return (it != _vars.end()) ? it->second.valueString : String("");
}

const Variable* VariableManager::get(const String& id) const {
    auto it = _vars.find(id);
    return (it != _vars.end()) ? &it->second : nullptr;
}

std::vector<const Variable*> VariableManager::all() const {
    std::vector<const Variable*> result;
    for (auto& kv : _vars) result.push_back(&kv.second);
    return result;
}

// ── Setters ───────────────────────────────────────────────────────────────────

void VariableManager::setInt(const String& id, int32_t value) {
    auto it = _vars.find(id);
    if (it == _vars.end()) return;
    if (it->second.valueInt == value) return;
    it->second.valueInt = value;
    notifyChange(it->second);
}

void VariableManager::setFloat(const String& id, float value) {
    auto it = _vars.find(id);
    if (it == _vars.end()) return;
    if (it->second.valueFloat == value) return;
    it->second.valueFloat = value;
    notifyChange(it->second);
}

void VariableManager::setBool(const String& id, bool value) {
    auto it = _vars.find(id);
    if (it == _vars.end()) return;
    if (it->second.valueBool == value) return;
    it->second.valueBool = value;
    notifyChange(it->second);
}

void VariableManager::setString(const String& id, const String& value) {
    auto it = _vars.find(id);
    if (it == _vars.end()) return;
    if (it->second.valueString == value) return;
    it->second.valueString = value;
    notifyChange(it->second);
}

void VariableManager::subscribe(const String& id, ChangeCallback cb) {
    _callbacks[id].push_back(std::move(cb));
}

void VariableManager::notifyChange(const Variable& var) {
    // Notify per-variable subscribers
    auto it = _callbacks.find(var.id);
    if (it != _callbacks.end()) {
        for (auto& cb : it->second) cb(var);
    }
    // Publish to global event bus
    JsonDocument doc;
    doc["id"] = var.id;
    String payload;
    serializeJson(doc, payload);
    EventBus::instance().publish(EventType::VariableChanged, var.id, payload);
}

// ── Serialisation ─────────────────────────────────────────────────────────────

void VariableManager::toJson(JsonDocument& doc) const {
    auto arr = doc["variables"].to<JsonArray>();
    for (auto& kv : _vars) {
        const auto& v = kv.second;
        if (!v.persistent) continue;
        auto obj        = arr.add<JsonObject>();
        obj["id"]       = v.id;
        obj["type"]     = static_cast<uint8_t>(v.type);
        obj["label"]    = v.label;
        obj["persistent"]= v.persistent;
        switch (v.type) {
            case VarType::Integer: obj["value"] = v.valueInt;    break;
            case VarType::Float:   obj["value"] = v.valueFloat;  break;
            case VarType::Boolean: obj["value"] = v.valueBool;   break;
            case VarType::String:  obj["value"] = v.valueString; break;
        }
    }
}

bool VariableManager::fromJson(const JsonDocument& doc) {
    if (!doc["variables"].is<JsonArray>()) return false;
    for (auto item : doc["variables"].as<JsonArray>()) {
        Variable v;
        v.id         = item["id"]    | String("");
        v.type       = static_cast<VarType>(item["type"] | 0);
        v.label      = item["label"] | String("");
        v.persistent = item["persistent"] | true;
        switch (v.type) {
            case VarType::Integer: v.valueInt    = item["value"] | 0;       break;
            case VarType::Float:   v.valueFloat  = item["value"] | 0.0f;   break;
            case VarType::Boolean: v.valueBool   = item["value"] | false;   break;
            case VarType::String:  v.valueString = item["value"] | String(""); break;
        }
        if (v.id.length()) _vars[v.id] = v;
    }
    return true;
}
