#include "InputManager.h"
#include <map>
#include "../core/PlatformCompat.h"
#include "../managers/VariableManager.h"

InputManager& InputManager::instance() {
    static InputManager inst;
    return inst;
}

bool InputManager::begin() {
    reload();
    LOG_I(TAG, "Initialised (" + String(_digitalConfigs.size()) + " digital, " + String(_analogConfigs.size()) + " analog)");
    return true;
}

// Re-read inputs.json and re-apply pin modes. Inputs are polled (no ISRs to detach),
// and nothing on the web task reads the input vectors, so this just needs to run on
// the loop task — which it does, via the flag consumed in loop().
void InputManager::reload() {
    if (!loadConfig()) {
        LOG_W(TAG, "Using empty input configuration");
    }
    configureDigitalPins();
    configureEncoderPins();
    configureKeypadPins();
    _digitalStates.assign(_digitalConfigs.size(), DigitalInputState{});
    _analogStates.assign(_analogConfigs.size(), AnalogInputState{});
    registerVariables();
}

void InputManager::requestReload() {
    _reloadPending = true;   // consumed in loop() on the main task
}

// ── F1: read-only variable mirror ───────────────────────────────────────────────
#if OF_ENABLE_HW_VARIABLES
void InputManager::registerVariables() {
    auto& vm = VariableManager::instance();
    // Purge stale mirrors before re-defining: reload() re-runs this and define()
    // alone never removes anything, so a deleted/renamed input would otherwise
    // leave a frozen variable behind until reboot. Removal is by the exact ids
    // we defined last time — a blanket input.* sweep could take out foreign
    // variables (e.g. an HA import whose user-chosen id lands in the namespace).
    for (const auto& id : _definedVarIds) vm.remove(id);
    _definedVarIds.clear();
    auto def = [&](const String& id, VarType type, const String& label) {
        // Track only ids define() actually creates: on a collision define() is a
        // no-op, and claiming a foreign (possibly persistent, user-created)
        // variable would delete it on the next reload — remove() doesn't spare
        // persistent variables the way the old removeByPrefix sweep did.
        if (vm.get(id)) return;
        vm.define(id, type, label, false, true);
        _definedVarIds.push_back(id);
    };
    for (const auto& c : _digitalConfigs)
        def("input." + c.id + ".pressed", VarType::Boolean, c.id + " pressed");
    for (const auto& c : _analogConfigs)
        def("input." + c.id + ".value", VarType::Integer, c.id + " value");
    for (const auto& c : _encoderConfigs)
        if (c.pinButton) def("input." + c.id + ".pressed", VarType::Boolean, c.id + " pressed");
    for (const auto& c : _keypadConfigs)
        def("input." + c.id + ".key", VarType::String, c.id + " key");
}
#else
void InputManager::registerVariables() {}
#endif

void InputManager::loop() {
    if (_reloadPending) {
        _reloadPending = false;
        reload();
    }
    const uint32_t nowMs = millis();
    updateDigitalInputs(nowMs);
    updateAnalogInputs(nowMs);
    updateEncoders(nowMs);
    updateKeypads(nowMs);
}

bool InputManager::loadConfig() {
    _digitalConfigs.clear();
    _analogConfigs.clear();
    _encoderConfigs.clear();
    _keypadConfigs.clear();
    std::map<String, uint16_t> idUsage;

    auto ensureUniqueId = [&](const String& requested, const String& fallbackPrefix, uint8_t pin) {
        String base = requested;
        if (!base.length()) base = fallbackPrefix + "_" + String(pin);
        if (!idUsage.count(base)) {
            idUsage[base] = 1;
            return base;
        }

        uint16_t suffix = idUsage[base];
        String candidate;
        do {
            candidate = base + "_" + String(suffix++);
        } while (idUsage.count(candidate));
        idUsage[base] = suffix;
        idUsage[candidate] = 1;
        LOG_W(TAG, "Duplicate input id '" + base + "' remapped to '" + candidate + "'");
        return candidate;
    };

    JsonDocument doc;
    if (!StorageManager::instance().readJson(OF_INPUTS_PATH, doc)) {
        return false;
    }

    if (doc["digital"].is<JsonArray>()) {
        for (auto item : doc["digital"].as<JsonArray>()) {
            DigitalInputConfig cfg;
            cfg.id                = item["id"] | String("");
            cfg.pin               = item["pin"] | 0;
            cfg.inverted          = item["inverted"] | false;
            cfg.pullup            = item["pullup"] | true;
            cfg.pulldown          = item["pulldown"] | false;
            cfg.touch             = item["touch"] | false;
            cfg.touchThreshold    = item["touch_threshold"] | 40;
            cfg.debounceMs        = item["debounce_ms"] | 30;
            cfg.holdMs            = item["hold_ms"] | 500;
            cfg.longPressMs       = item["long_press_ms"] | 1200;
            cfg.multiPressWindowMs= item["multi_press_window_ms"] | 350;
            cfg.repeatStartMs     = item["repeat_start_ms"] | 800;
            cfg.repeatIntervalMs  = item["repeat_interval_ms"] | 250;
            cfg.id = ensureUniqueId(cfg.id, "din", cfg.pin);
            _digitalConfigs.push_back(cfg);
        }
    }

    if (doc["analog"].is<JsonArray>()) {
        for (auto item : doc["analog"].as<JsonArray>()) {
            AnalogInputConfig cfg;
            cfg.id                 = item["id"] | String("");
            cfg.pin                = item["pin"] | 0;
            cfg.inverted           = item["inverted"] | false;
            cfg.minDelta           = item["min_delta"] | 8;
            cfg.thresholdEnabled   = item["threshold_enabled"] | false;
            cfg.threshold          = item["threshold"] | 2048;
            cfg.thresholdHysteresis= item["threshold_hysteresis"] | 16;
            cfg.rangeEnabled       = item["range_enabled"] | false;
            cfg.rangeMin           = item["range_min"] | 0;
            cfg.rangeMax           = item["range_max"] | 4095;
            cfg.pollIntervalMs     = item["poll_interval_ms"] | 30;
            cfg.id = ensureUniqueId(cfg.id, "ain", cfg.pin);
            _analogConfigs.push_back(cfg);
        }
    }

    if (doc["encoders"].is<JsonArray>()) {
        for (auto item : doc["encoders"].as<JsonArray>()) {
            EncoderInputConfig cfg;
            cfg.pinA       = item["pin_a"] | 0;
            cfg.pinB       = item["pin_b"] | 0;
            cfg.pinButton  = item["pin_button"] | 0;
            cfg.pullup     = item["pullup"] | true;
            cfg.debounceMs = item["debounce_ms"] | 30;
            cfg.id = ensureUniqueId(item["id"] | String(""), "enc", cfg.pinA);
            _encoderConfigs.push_back(cfg);
        }
    }
    _encoderStates.assign(_encoderConfigs.size(), EncoderInputState{});

    if (doc["keypads"].is<JsonArray>()) {
        for (auto item : doc["keypads"].as<JsonArray>()) {
            KeypadInputConfig cfg;
            for (JsonVariantConst v : item["rows"].as<JsonArrayConst>()) cfg.rows.push_back(v | 0);
            for (JsonVariantConst v : item["cols"].as<JsonArrayConst>()) cfg.cols.push_back(v | 0);
            for (JsonVariantConst v : item["keys"].as<JsonArrayConst>()) cfg.keys.push_back(v | String(""));
            cfg.debounceMs = item["debounce_ms"] | 30;
            cfg.id = ensureUniqueId(item["id"] | String(""), "keypad", 0);
            if (!cfg.rows.empty() && !cfg.cols.empty()) _keypadConfigs.push_back(cfg);
        }
    }
    _keypadStates.clear();
    for (const auto& cfg : _keypadConfigs) {
        KeypadInputState st;
        st.pressed.assign(cfg.rows.size() * cfg.cols.size(), false);
        st.lastChange.assign(cfg.rows.size() * cfg.cols.size(), 0);
        _keypadStates.push_back(st);
    }

    return true;
}

void InputManager::configureDigitalPins() {
    for (const auto& cfg : _digitalConfigs) {
        if (cfg.touch) {
            continue;  // touch pins need no pinMode — touchRead() drives the pad
        }
        if (cfg.pullup) {
            pinMode(cfg.pin, INPUT_PULLUP);
        } else if (cfg.pulldown) {
            pinMode(cfg.pin, INPUT_PULLDOWN);
        } else {
            pinMode(cfg.pin, INPUT);
        }
    }
}

void InputManager::updateDigitalInputs(uint32_t nowMs) {
    for (size_t i = 0; i < _digitalConfigs.size(); ++i) {
        updateDigitalInput(i, nowMs);
    }
}

void InputManager::updateDigitalInput(size_t index, uint32_t nowMs) {
    auto& cfg   = _digitalConfigs[index];
    auto& state = _digitalStates[index];

    const bool rawPressed = readDigitalPressed(cfg);
    if (rawPressed != state.lastRawPressed) {
        state.lastRawPressed = rawPressed;
        state.lastDebounceChange = nowMs;
    }

    if ((nowMs - state.lastDebounceChange) < cfg.debounceMs) {
        return;
    }

    if (rawPressed != state.stablePressed) {
        state.stablePressed = rawPressed;
        if (state.stablePressed) {
            state.pressStartMs = nowMs;
            state.holdEmitted = false;
            state.longPressEmitted = false;
            state.lastRepeatMs = nowMs;
            emitDigitalEvent(cfg, "Press");
        } else {
            const uint32_t duration = nowMs - state.pressStartMs;
            emitDigitalEvent(cfg, "Release", duration);
            state.clickCount++;
            state.lastReleaseMs = nowMs;
        }
    }

    if (!state.stablePressed && state.clickCount > 0 && (nowMs - state.lastReleaseMs) >= cfg.multiPressWindowMs) {
        if (state.clickCount == 2) {
            emitDigitalEvent(cfg, "DoublePress", 0, state.clickCount);
        } else if (state.clickCount >= 3) {
            emitDigitalEvent(cfg, "TriplePress", 0, state.clickCount);
        }
        state.clickCount = 0;
    }

    if (!state.stablePressed) return;

    const uint32_t heldMs = nowMs - state.pressStartMs;
    if (!state.holdEmitted && heldMs >= cfg.holdMs) {
        state.holdEmitted = true;
        emitDigitalEvent(cfg, "Hold", heldMs);
    }
    if (!state.longPressEmitted && heldMs >= cfg.longPressMs) {
        state.longPressEmitted = true;
        emitDigitalEvent(cfg, "LongPress", heldMs);
    }

    if (heldMs >= cfg.repeatStartMs && cfg.repeatIntervalMs > 0) {
        if ((nowMs - state.lastRepeatMs) >= cfg.repeatIntervalMs) {
            state.lastRepeatMs = nowMs;
            emitDigitalEvent(cfg, "Repeat", heldMs);
        }
    }
}

bool InputManager::readDigitalPressed(const DigitalInputConfig& cfg) const {
#if defined(ESP32) && !defined(ESP8266_BOARD) && !defined(CONFIG_IDF_TARGET_ESP32C3)
    if (cfg.touch) {
        // touchRead() falls as the pad is touched; below threshold = pressed.
        // (The ESP32-C3 has no capacitive-touch peripheral, hence the guard.)
        bool pressed = touchRead(cfg.pin) < cfg.touchThreshold;
        return cfg.inverted ? !pressed : pressed;
    }
#else
    // No capacitive-touch peripheral on this target. configureDigitalPins() skips
    // pinMode() for touch pins, so falling through to digitalRead() would read a
    // floating pin and emit phantom presses — report not-pressed instead.
    if (cfg.touch) return false;
#endif
    const int raw = digitalRead(cfg.pin);
    bool pressed = (raw == HIGH);
    if (cfg.pullup && !cfg.pulldown) {
        pressed = (raw == LOW);
    }
    if (cfg.inverted) {
        pressed = !pressed;
    }
    return pressed;
}

void InputManager::emitDigitalEvent(const DigitalInputConfig& cfg, const char* eventName, uint32_t durationMs, uint8_t pressCount) {
    JsonDocument doc;
    doc["event"] = eventName;
    doc["id"] = cfg.id;
    doc["pin"] = cfg.pin;
    if (durationMs > 0) {
        doc["duration_ms"] = durationMs;
    }
    if (pressCount > 0) {
        doc["count"] = pressCount;
    }

    String payload;
    serializeJson(doc, payload);
    EventBus::instance().publish(EventType::InputDigitalChanged, cfg.id, payload);

#if OF_ENABLE_HW_VARIABLES
    if      (strcmp(eventName, "Press") == 0)   VariableManager::instance().setBool("input." + cfg.id + ".pressed", true);
    else if (strcmp(eventName, "Release") == 0) VariableManager::instance().setBool("input." + cfg.id + ".pressed", false);
#endif
}

void InputManager::configureEncoderPins() {
    for (const auto& cfg : _encoderConfigs) {
        const int mode = cfg.pullup ? INPUT_PULLUP : INPUT;
        pinMode(cfg.pinA, mode);
        pinMode(cfg.pinB, mode);
        if (cfg.pinButton) pinMode(cfg.pinButton, mode);
    }
}

void InputManager::emitEncoderEvent(const EncoderInputConfig& cfg, const char* eventName) {
    JsonDocument doc;
    doc["event"] = eventName;
    doc["id"]    = cfg.id;
    String payload;
    serializeJson(doc, payload);
    EventBus::instance().publish(EventType::InputDigitalChanged, cfg.id, payload);

#if OF_ENABLE_HW_VARIABLES
    if      (strcmp(eventName, "Press") == 0)   VariableManager::instance().setBool("input." + cfg.id + ".pressed", true);
    else if (strcmp(eventName, "Release") == 0) VariableManager::instance().setBool("input." + cfg.id + ".pressed", false);
#endif
}

void InputManager::updateEncoders(uint32_t nowMs) {
    // Standard quadrature transition table: index by (prevAB<<2 | curAB) → -1/0/+1.
    static const int8_t kTransitions[16] = {
        0, -1,  1,  0,
        1,  0,  0, -1,
       -1,  0,  0,  1,
        0,  1, -1,  0,
    };
    for (size_t i = 0; i < _encoderConfigs.size(); ++i) {
        const auto& cfg = _encoderConfigs[i];
        auto& st = _encoderStates[i];

        const uint8_t ab = (digitalRead(cfg.pinA) ? 2 : 0) | (digitalRead(cfg.pinB) ? 1 : 0);
        const uint8_t idx = (st.lastAB << 2) | ab;
        st.accum += kTransitions[idx & 0x0F];
        st.lastAB = ab;
        // Four quarter-steps make one detent on common encoders; emit per detent.
        if (st.accum >= 4)       { emitEncoderEvent(cfg, "RotateCW");  st.accum = 0; }
        else if (st.accum <= -4) { emitEncoderEvent(cfg, "RotateCCW"); st.accum = 0; }

        // Button: active-low when pulled up. Debounced press/release.
        if (cfg.pinButton) {
            const bool raw = cfg.pullup ? (digitalRead(cfg.pinButton) == LOW)
                                        : (digitalRead(cfg.pinButton) == HIGH);
            if (raw != st.buttonRaw) { st.buttonRaw = raw; st.buttonChangeMs = nowMs; }
            if (raw != st.buttonPressed && (nowMs - st.buttonChangeMs) >= cfg.debounceMs) {
                st.buttonPressed = raw;
                emitEncoderEvent(cfg, raw ? "Press" : "Release");
            }
        }
    }
}

void InputManager::configureKeypadPins() {
    for (const auto& cfg : _keypadConfigs) {
        for (uint8_t r : cfg.rows) { pinMode(r, OUTPUT); digitalWrite(r, HIGH); }
        for (uint8_t c : cfg.cols) { pinMode(c, INPUT_PULLUP); }
    }
}

void InputManager::emitKeypadEvent(const KeypadInputConfig& cfg, const String& key, const char* eventName) {
    JsonDocument doc;
    doc["event"] = eventName;
    doc["id"]    = cfg.id;
    doc["key"]   = key;
    String payload;
    serializeJson(doc, payload);
    EventBus::instance().publish(EventType::InputDigitalChanged, cfg.id, payload);

#if OF_ENABLE_HW_VARIABLES
    if (strcmp(eventName, "KeyPress") == 0) VariableManager::instance().setString("input." + cfg.id + ".key", key);
#endif
}

void InputManager::updateKeypads(uint32_t nowMs) {
    for (size_t k = 0; k < _keypadConfigs.size(); ++k) {
        const auto& cfg = _keypadConfigs[k];
        auto& st = _keypadStates[k];
        const size_t cols = cfg.cols.size();

        for (size_t ri = 0; ri < cfg.rows.size(); ++ri) {
            digitalWrite(cfg.rows[ri], LOW);        // activate this row
            delayMicroseconds(5);                   // let the column lines settle
            for (size_t ci = 0; ci < cols; ++ci) {
                const bool down = digitalRead(cfg.cols[ci]) == LOW;  // pulled-up → LOW = pressed
                const size_t idx = ri * cols + ci;
                if (down != st.pressed[idx] && (nowMs - st.lastChange[idx]) >= cfg.debounceMs) {
                    st.pressed[idx] = down;
                    st.lastChange[idx] = nowMs;
                    const String key = idx < cfg.keys.size() ? cfg.keys[idx]
                                                             : String(ri) + "," + String(ci);
                    emitKeypadEvent(cfg, key, down ? "KeyPress" : "KeyRelease");
                }
            }
            digitalWrite(cfg.rows[ri], HIGH);       // deactivate the row
        }
    }
}

void InputManager::updateAnalogInputs(uint32_t nowMs) {
    for (size_t i = 0; i < _analogConfigs.size(); ++i) {
        updateAnalogInput(i, nowMs);
    }
}

void InputManager::updateAnalogInput(size_t index, uint32_t nowMs) {
    auto& cfg   = _analogConfigs[index];
    auto& state = _analogStates[index];

    if (cfg.pollIntervalMs > 0 && (nowMs - state.lastPollMs) < cfg.pollIntervalMs) {
        return;
    }
    state.lastPollMs = nowMs;

    const uint16_t value = readAnalogValue(cfg);
    if (!state.initialized) {
        state.initialized = true;
        state.lastValue = value;
        state.aboveThreshold = value >= cfg.threshold;
        state.inRange = value >= cfg.rangeMin && value <= cfg.rangeMax;
        emitAnalogEvent(cfg, "ValueChanged", value);
        return;
    }

    const uint16_t delta = (value > state.lastValue) ? (value - state.lastValue) : (state.lastValue - value);
    if (delta >= cfg.minDelta) {
        state.lastValue = value;
        emitAnalogEvent(cfg, "ValueChanged", value);
    }

    if (cfg.thresholdEnabled) {
        // Sticky hysteresis band: enter-above at threshold + hysteresis, exit-above at threshold - hysteresis.
        const bool nowAbove = state.aboveThreshold
            ? (value >= (cfg.threshold - cfg.thresholdHysteresis))
            : (value >= (cfg.threshold + cfg.thresholdHysteresis));
        if (nowAbove != state.aboveThreshold) {
            state.aboveThreshold = nowAbove;
            emitAnalogEvent(cfg, "ThresholdReached", value, nowAbove ? "rising" : "falling");
        }
    }

    if (cfg.rangeEnabled) {
        const bool nowInRange = value >= cfg.rangeMin && value <= cfg.rangeMax;
        if (nowInRange != state.inRange) {
            state.inRange = nowInRange;
            emitAnalogEvent(cfg, nowInRange ? "RangeEntered" : "RangeExited", value);
        }
    }
}

uint16_t InputManager::readAnalogValue(const AnalogInputConfig& cfg) const {
    uint16_t value = analogRead(cfg.pin);
    if (cfg.inverted) {
        value = 4095 - value;
    }
    return value;
}

void InputManager::emitAnalogEvent(const AnalogInputConfig& cfg, const char* eventName, uint16_t value, const char* state) {
    JsonDocument doc;
    doc["event"] = eventName;
    doc["id"] = cfg.id;
    doc["pin"] = cfg.pin;
    doc["value"] = value;
    if (state != nullptr) {
        doc["state"] = state;
    }

    String payload;
    serializeJson(doc, payload);
    EventBus::instance().publish(EventType::InputAnalogChanged, cfg.id, payload);

#if OF_ENABLE_HW_VARIABLES
    VariableManager::instance().setInt("input." + cfg.id + ".value", value);
#endif
}
