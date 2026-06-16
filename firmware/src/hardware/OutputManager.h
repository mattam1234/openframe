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

// Runtime animation modes for addressable (WS2812) strips.
enum class LedAnimation : uint8_t {
    Solid,      // every LED shows the current colour
    Off,        // all LEDs black (remembers colour/animation for resume)
    Blink,      // hard on/off toggle
    Breathe,    // smooth brightness pulse of the current colour
    Rainbow,    // hue sweep across the strip, scrolling
    Chase,      // a single lit pixel running along the strip
    ColorWipe,  // progressively fill then clear with the current colour
};

const char* ledAnimationToString(LedAnimation a);
LedAnimation ledAnimationFromString(const String& s);

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

    // WS2812 animation runtime
    LedAnimation animation     = LedAnimation::Solid;
    uint8_t      animationSpeed = 128;  // 1 (slow) … 255 (fast)
    uint16_t     animPhase      = 0;
    uint32_t     animLastMs     = 0;
};

class OutputManager {
public:
    static OutputManager& instance();

    bool begin();
    void loop();

    bool setDigital(const String& id, bool on);
    bool setBrightness(const String& id, uint8_t brightness);
    bool setRgb(const String& id, uint8_t r, uint8_t g, uint8_t b);
    bool setAnimation(const String& id, LedAnimation animation, uint8_t speed);
    bool beep(const String& id, uint16_t frequency, uint16_t durationMs);

    // Serialise the live state of every output (id, type, on, colour, animation…)
    // into the given array — used by the control UI to render and stay in sync.
    void fillStateJson(JsonArray& arr) const;

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
    void renderWs2812(size_t index);
    void tickAnimations(uint32_t nowMs);
    void emitOutputEvent(size_t index, const char* action);

    static const char* outputTypeToString(OutputType type);

    std::vector<OutputConfig> _configs;
    std::vector<OutputState>  _states;
    std::vector<std::vector<CRGB>> _wsLedsPerOutput;

    static constexpr const char* TAG = "OutputMgr";
};
