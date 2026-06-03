#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <vector>
#include <FastLED.h>
#include "../core/StorageManager.h"
#include "../core/EventBus.h"
#include "../core/Logger.h"
#include "../OpenFrameConfig.h"

enum class OutputType : uint8_t {
    Led,
    Rgb,
    Ws2812,
    Relay,
    Buzzer,
};

struct OutputConfig {
    String     id;
    OutputType type            = OutputType::Led;
    bool       inverted        = false;
    uint8_t    pin             = 0;

    // LED/relay/buzzer
    bool       pwm             = false;
    uint8_t    pwmChannel      = 0;
    uint16_t   pwmFrequency    = 5000;
    uint8_t    pwmResolution   = 8;

    // RGB
    uint8_t    pinR            = 0;
    uint8_t    pinG            = 0;
    uint8_t    pinB            = 0;
    uint8_t    channelR        = 0;
    uint8_t    channelG        = 1;
    uint8_t    channelB        = 2;

    // WS2812
    uint16_t   ledCount        = 1;
    uint8_t    brightness      = 255;
};

struct OutputState {
    bool      on          = false;
    uint8_t   brightness  = 0;
    uint8_t   red         = 0;
    uint8_t   green       = 0;
    uint8_t   blue        = 0;
    uint16_t  buzzerFreq  = 0;
    uint32_t  buzzerUntil = 0;
    bool      wsReady     = false;
};

class OutputManager {
public:
    static OutputManager& instance();

    bool begin();
    void loop();

    bool setDigital(const String& id, bool on);
    bool setBrightness(const String& id, uint8_t brightness);
    bool setRgb(const String& id, uint8_t r, uint8_t g, uint8_t b);
    bool beep(const String& id, uint16_t frequency, uint16_t durationMs);

private:
    OutputManager() = default;

    bool loadConfig();
    void setupOutputs();
    void setupOutput(size_t index);

    bool applyDigital(size_t index, bool on);
    bool applyBrightness(size_t index, uint8_t brightness);
    bool applyRgb(size_t index, uint8_t r, uint8_t g, uint8_t b);
    bool applyBuzzer(size_t index, uint16_t frequency, uint16_t durationMs);

    int findIndexById(const String& id) const;

    bool initWs2812(size_t index);
    void updateWs2812(size_t index);
    void emitOutputEvent(size_t index, const char* action);

    std::vector<OutputConfig> _configs;
    std::vector<OutputState>  _states;
    std::vector<std::vector<CRGB>> _wsLedsPerOutput;

    static constexpr const char* TAG = "OutputMgr";
};
