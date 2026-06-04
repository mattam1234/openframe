#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <map>
#include <vector>
#include <functional>
#include "../core/StorageManager.h"
#include "../core/EventBus.h"
#include "../core/Logger.h"
#include "../OpenFrameConfig.h"

// ── Variable types ────────────────────────────────────────────────────────────

enum class VarType : uint8_t { Integer, Float, Boolean, String };

struct Variable {
    String  id;
    VarType type;
    // Storage — only one of these is active at a time
    int32_t valueInt    = 0;
    float   valueFloat  = 0.0f;
    bool    valueBool   = false;
    String  valueString;
    // Metadata
    String  label;
    bool    persistent = true;
};

// ── VariableManager ───────────────────────────────────────────────────────────

class VariableManager {
public:
    using ChangeCallback = std::function<void(const Variable&)>;

    static VariableManager& instance();

    bool begin();
    bool save();

    // Define a variable (idempotent — only registers if not already present)
    void define(const String& id, VarType type, const String& label = "", bool persistent = true);

    // Getters
    int32_t getInt(const String& id) const;
    float   getFloat(const String& id) const;
    bool    getBool(const String& id) const;
    String  getString(const String& id) const;

    // Setters — emit VariableChanged event on change
    void setInt(const String& id, int32_t value);
    void setFloat(const String& id, float value);
    void setBool(const String& id, bool value);
    void setString(const String& id, const String& value);

    // Subscribe to changes for a specific variable
    void subscribe(const String& id, ChangeCallback cb);

    const Variable* get(const String& id) const;
    std::vector<const Variable*> all() const;

    void toJson(JsonDocument& doc) const;
    bool fromJson(const JsonDocument& doc);

private:
    VariableManager() = default;
    void notifyChange(const Variable& var);

    std::map<String, Variable>                       _vars;
    std::map<String, std::vector<ChangeCallback>>    _callbacks;
    static constexpr const char* TAG = "Variables";
};
