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
    // ESP32 native capacitive touch (#16): when set, the pin is read via
    // touchRead() and counts as "pressed" below touchThreshold. Reuses all the
    // press/hold/multi-press logic. No effect on ESP8266 (no touch peripheral).
    bool     touch              = false;
    uint16_t touchThreshold     = 40;
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

// Rotary encoder with optional push-button (#15). Quadrature A/B are polled and
// decoded to RotateCW/RotateCCW events; the button emits Press/Release.
struct EncoderInputConfig {
    String  id;
    uint8_t pinA       = 0;
    uint8_t pinB       = 0;
    uint8_t pinButton  = 0;   // 0 = no button
    bool    pullup     = true;
    uint32_t debounceMs = 30; // button debounce
};

// Matrix keypad (#21): rows driven low one at a time, columns read with pull-ups.
// `keys` is row-major (rows×cols) labels; a press emits KeyPress with the label.
struct KeypadInputConfig {
    String               id;
    std::vector<uint8_t> rows;
    std::vector<uint8_t> cols;
    std::vector<String>  keys;       // size rows*cols, row-major
    uint32_t             debounceMs = 30;
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

    struct EncoderInputState {
        uint8_t  lastAB         = 0;
        int8_t   accum          = 0;     // accumulates quarter-steps to one detent
        bool     buttonPressed  = false;
        bool     buttonRaw      = false;
        uint32_t buttonChangeMs = 0;
    };

    struct KeypadInputState {
        std::vector<bool>     pressed;     // per-key stable state (rows*cols)
        std::vector<uint32_t> lastChange;  // per-key debounce timer
    };

    InputManager() = default;

    bool loadConfig();
    // F1 — define read-only mirror variables (input.<id>.pressed/value/key) for
    // every configured input. No-op when OF_ENABLE_HW_VARIABLES is off.
    void registerVariables();

    void configureDigitalPins();
    void updateDigitalInputs(uint32_t nowMs);
    void updateDigitalInput(size_t index, uint32_t nowMs);
    bool readDigitalPressed(const DigitalInputConfig& cfg) const;
    void emitDigitalEvent(const DigitalInputConfig& cfg, const char* eventName, uint32_t durationMs = 0, uint8_t pressCount = 0);

    void updateAnalogInputs(uint32_t nowMs);
    void updateAnalogInput(size_t index, uint32_t nowMs);
    uint16_t readAnalogValue(const AnalogInputConfig& cfg) const;
    void emitAnalogEvent(const AnalogInputConfig& cfg, const char* eventName, uint16_t value, const char* state = nullptr);

    void configureEncoderPins();
    void updateEncoders(uint32_t nowMs);
    void emitEncoderEvent(const EncoderInputConfig& cfg, const char* eventName);

    void configureKeypadPins();
    void updateKeypads(uint32_t nowMs);
    void emitKeypadEvent(const KeypadInputConfig& cfg, const String& key, const char* eventName);

    std::vector<DigitalInputConfig> _digitalConfigs;
    std::vector<DigitalInputState>  _digitalStates;
    std::vector<AnalogInputConfig>  _analogConfigs;
    std::vector<AnalogInputState>   _analogStates;
    std::vector<EncoderInputConfig> _encoderConfigs;
    std::vector<EncoderInputState>  _encoderStates;
    std::vector<KeypadInputConfig>  _keypadConfigs;
    std::vector<KeypadInputState>   _keypadStates;

    static constexpr const char* TAG = "InputMgr";
};
