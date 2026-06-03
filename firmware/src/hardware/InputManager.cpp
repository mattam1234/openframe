#include "InputManager.h"

InputManager& InputManager::instance() {
    static InputManager inst;
    return inst;
}

bool InputManager::begin() {
    if (!loadConfig()) {
        LOG_W(TAG, "Using empty input configuration");
    }

    configureDigitalPins();
    _digitalStates.resize(_digitalConfigs.size());
    _analogStates.resize(_analogConfigs.size());

    LOG_I(TAG, "Initialised (" + String(_digitalConfigs.size()) + " digital, " + String(_analogConfigs.size()) + " analog)");
    return true;
}

void InputManager::loop() {
    const uint32_t nowMs = millis();
    updateDigitalInputs(nowMs);
    updateAnalogInputs(nowMs);
}

bool InputManager::loadConfig() {
    _digitalConfigs.clear();
    _analogConfigs.clear();

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
            cfg.debounceMs        = item["debounce_ms"] | 30;
            cfg.holdMs            = item["hold_ms"] | 500;
            cfg.longPressMs       = item["long_press_ms"] | 1200;
            cfg.multiPressWindowMs= item["multi_press_window_ms"] | 350;
            cfg.repeatStartMs     = item["repeat_start_ms"] | 800;
            cfg.repeatIntervalMs  = item["repeat_interval_ms"] | 250;
            if (!cfg.id.length()) cfg.id = "din_" + String(cfg.pin);
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
            if (!cfg.id.length()) cfg.id = "ain_" + String(cfg.pin);
            _analogConfigs.push_back(cfg);
        }
    }

    return true;
}

void InputManager::configureDigitalPins() {
    for (const auto& cfg : _digitalConfigs) {
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
}
