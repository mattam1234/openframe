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

void VariableManager::define(const String& id, VarType type, const String& label, bool persistent,
                             bool readOnly) {
    if (_vars.count(id)) return;
    Variable v;
    v.id         = id;
    v.type       = type;
    v.label      = label;
    v.persistent = persistent;
    v.readOnly   = readOnly;
    _vars[id]    = v;
}

void VariableManager::setMeta(const String& id, float min, float max, const String& unit) {
    auto it = _vars.find(id);
    if (it == _vars.end()) return;
    it->second.hasRange = true;
    it->second.rangeMin = min;
    it->second.rangeMax = max;
    if (unit.length()) it->second.unit = unit;
}

void VariableManager::setOptions(const String& id, const std::vector<String>& options) {
    auto it = _vars.find(id);
    if (it == _vars.end()) return;
    it->second.options = options;
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
    if (it->second.hasRange) {
        if (value < it->second.rangeMin) value = static_cast<int32_t>(it->second.rangeMin);
        if (value > it->second.rangeMax) value = static_cast<int32_t>(it->second.rangeMax);
    }
    if (it->second.valueInt == value) return;
    it->second.valueInt = value;
    notifyChange(it->second);
}

void VariableManager::setFloat(const String& id, float value) {
    auto it = _vars.find(id);
    if (it == _vars.end()) return;
    if (it->second.hasRange) {
        if (value < it->second.rangeMin) value = it->second.rangeMin;
        if (value > it->second.rangeMax) value = it->second.rangeMax;
    }
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
    // Enum: reject values outside the declared option set.
    if (!it->second.options.empty()) {
        bool ok = false;
        for (const auto& opt : it->second.options) { if (opt == value) { ok = true; break; } }
        if (!ok) {
            LOG_W(TAG, "Rejected enum value '" + value + "' for " + id);
            return;
        }
    }
    if (it->second.valueString == value) return;
    it->second.valueString = value;
    notifyChange(it->second);
}

void VariableManager::subscribe(const String& id, ChangeCallback cb) {
    _callbacks[id].push_back(std::move(cb));
}

void VariableManager::loop() {
    if (!_dirty) return;
    const uint32_t now = millis();
    if (now - _lastFlushMs < FLUSH_INTERVAL_MS) return;
    _lastFlushMs = now;
    if (save()) {
        _dirty = false;
        LOG_D(TAG, "Flushed persistent variables");
    }
}

void VariableManager::notifyChange(const Variable& var) {
    // Mark for the next periodic flush so the change survives an unexpected reset.
    if (var.persistent) _dirty = true;

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
        if (v.hasRange) { obj["min"] = v.rangeMin; obj["max"] = v.rangeMax; }
        if (v.unit.length()) obj["unit"] = v.unit;
        if (!v.options.empty()) {
            auto opts = obj["options"].to<JsonArray>();
            for (const auto& o : v.options) opts.add(o);
        }
        switch (v.type) {
            case VarType::Integer: obj["value"] = v.valueInt;    break;
            case VarType::Float:   obj["value"] = v.valueFloat;  break;
            case VarType::Boolean: obj["value"] = v.valueBool;   break;
            case VarType::String:  obj["value"] = v.valueString; break;
        }
    }
}

bool VariableManager::fromJson(const JsonDocument& doc) {
    if (!doc["variables"].is<JsonArrayConst>()) return false;
    JsonArrayConst variables = doc["variables"].as<JsonArrayConst>();
    for (JsonVariantConst item : variables) {
        Variable v;
        v.id         = item["id"]    | String("");
        v.type       = static_cast<VarType>(item["type"] | 0);
        v.label      = item["label"] | String("");
        v.persistent = item["persistent"] | true;
        if (item["min"].is<float>() || item["max"].is<float>() ||
            item["min"].is<int>()   || item["max"].is<int>()) {
            v.hasRange = true;
            v.rangeMin = item["min"] | 0.0f;
            v.rangeMax = item["max"] | 0.0f;
        }
        v.unit = item["unit"] | String("");
        if (item["options"].is<JsonArrayConst>()) {
            for (JsonVariantConst o : item["options"].as<JsonArrayConst>()) {
                const String s = o | String("");
                if (s.length()) v.options.push_back(s);
            }
        }
        switch (v.type) {
            case VarType::Integer: v.valueInt    = item["value"] | 0;          break;
            case VarType::Float:   v.valueFloat  = item["value"] | 0.0f;      break;
            case VarType::Boolean: v.valueBool   = item["value"] | false;      break;
            case VarType::String:  v.valueString = item["value"] | String(""); break;
        }
        if (v.id.length()) _vars[v.id] = v;
    }
    return true;
}
