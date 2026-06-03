#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <vector>
#include "../core/StorageManager.h"
#include "../core/EventBus.h"
#include "../core/Logger.h"
#include "../OpenFrameConfig.h"

struct DigitalInputConfig {
    String   id;
    uint8_t  pin                = 0;
    bool     inverted           = false;
    bool     pullup             = true;
    bool     pulldown           = false;
    uint32_t debounceMs         = 30;
    uint32_t holdMs             = 500;
    uint32_t longPressMs        = 1200;
    uint32_t multiPressWindowMs = 350;
    uint32_t repeatStartMs      = 800;
    uint32_t repeatIntervalMs   = 250;
};

struct AnalogInputConfig {
    String   id;
    uint8_t  pin                = 0;
    bool     inverted           = false;
    uint16_t minDelta           = 8;
    bool     thresholdEnabled   = false;
    uint16_t threshold          = 2048;
    uint16_t thresholdHysteresis= 16;
    bool     rangeEnabled       = false;
    uint16_t rangeMin           = 0;
    uint16_t rangeMax           = 4095;
    uint32_t pollIntervalMs     = 30;
};

class InputManager {
public:
    static InputManager& instance();

    bool begin();
    void loop();

private:
    struct DigitalInputState {
        bool     stablePressed      = false;
        bool     lastRawPressed     = false;
        uint32_t lastDebounceChange = 0;
        uint32_t pressStartMs       = 0;
        bool     holdEmitted        = false;
        bool     longPressEmitted   = false;
        uint32_t lastRepeatMs       = 0;
        uint8_t  clickCount         = 0;
        uint32_t lastReleaseMs      = 0;
    };

    struct AnalogInputState {
        uint16_t lastValue      = 0;
        bool     initialized    = false;
        bool     aboveThreshold = false;
        bool     inRange        = false;
        uint32_t lastPollMs     = 0;
    };

    InputManager() = default;

    bool loadConfig();

    void configureDigitalPins();
    void updateDigitalInputs(uint32_t nowMs);
    void updateDigitalInput(size_t index, uint32_t nowMs);
    bool readDigitalPressed(const DigitalInputConfig& cfg) const;
    void emitDigitalEvent(const DigitalInputConfig& cfg, const char* eventName, uint32_t durationMs = 0, uint8_t pressCount = 0);

    void updateAnalogInputs(uint32_t nowMs);
    void updateAnalogInput(size_t index, uint32_t nowMs);
    uint16_t readAnalogValue(const AnalogInputConfig& cfg) const;
    void emitAnalogEvent(const AnalogInputConfig& cfg, const char* eventName, uint16_t value, const char* state = nullptr);

    std::vector<DigitalInputConfig> _digitalConfigs;
    std::vector<DigitalInputState>  _digitalStates;
    std::vector<AnalogInputConfig>  _analogConfigs;
    std::vector<AnalogInputState>   _analogStates;

    static constexpr const char* TAG = "InputMgr";
};
