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
    // Read-only variables mirror live hardware state and reject API writes
    // (e.g. sensor metrics, input states). Writable hardware mirrors (output
    // properties) leave this false so a write drives the hardware. See F1.
    bool    readOnly   = false;
    // Optional typing constraints (#11):
    //  • range — clamps Integer/Float sets to [rangeMin, rangeMax]
    //  • unit  — display suffix (e.g. "°C", "%")
    //  • options — when non-empty, a String var is an enum; sets outside the set
    //    are rejected
    bool                 hasRange = false;
    float                rangeMin = 0.0f;
    float                rangeMax = 0.0f;
    String               unit;
    std::vector<String>  options;
};

// ── VariableManager ───────────────────────────────────────────────────────────

class VariableManager {
public:
    using ChangeCallback = std::function<void(const Variable&)>;

    static VariableManager& instance();

    bool begin();
    bool save();

    // Periodically flush persistent variables to LittleFS when they've changed,
    // so an unexpected reset (brownout / power loss) loses at most one interval of
    // updates rather than everything since the last explicit save.
    void loop();

    // Define a variable (idempotent — only registers if not already present)
    void define(const String& id, VarType type, const String& label = "", bool persistent = true,
                bool readOnly = false);

    // Attach range (clamps Integer/Float sets) and/or a display unit to an existing
    // variable. Used by the hardware auto-registry to give generated property
    // variables sensible bounds (e.g. brightness 0–255) and units. No-op if absent.
    void setMeta(const String& id, float min, float max, const String& unit = "");
    // Attach an enum option set to an existing String variable (e.g. animations).
    void setOptions(const String& id, const std::vector<String>& options);

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
    bool      _dirty       = false;   // a persistent var changed since last flush
    uint32_t  _lastFlushMs = 0;
    static constexpr uint32_t FLUSH_INTERVAL_MS = 30000;
    static constexpr const char* TAG = "Variables";
};
