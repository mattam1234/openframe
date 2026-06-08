#include "OutputManager.h"
#include <map>
#include "../core/PlatformCompat.h"

OutputManager& OutputManager::instance() {
    static OutputManager inst;
    return inst;
}

bool OutputManager::begin() {
    if (!loadConfig()) {
        LOG_W(TAG, "Using empty output configuration");
    }
    _states.assign(_configs.size(), OutputState{});
    _wsLedsPerOutput.assign(_configs.size(), std::vector<CRGB>{});
    setupOutputs();
    LOG_I(TAG, "Initialised (" + String(_configs.size()) + " outputs)");
    return true;
}

void OutputManager::loop() {
    const uint32_t nowMs = millis();
    for (size_t i = 0; i < _configs.size(); ++i) {
        const auto& cfg = _configs[i];
        auto& state = _states[i];
        if (cfg.type == OutputType::Buzzer && state.buzzerUntil > 0 && nowMs >= state.buzzerUntil) {
            state.buzzerUntil = 0;
            state.on = false;
            of_tone_write(cfg.pin, cfg.pwmChannel, 0);
            emitOutputEvent(i, "BuzzerStop");
        }
    }
}

bool OutputManager::setDigital(const String& id, bool on) {
    const int index = findIndexById(id);
    if (index < 0) return false;
    return applyDigital(static_cast<size_t>(index), on);
}

bool OutputManager::setBrightness(const String& id, uint8_t brightness) {
    const int index = findIndexById(id);
    if (index < 0) return false;
    return applyBrightness(static_cast<size_t>(index), brightness);
}

bool OutputManager::setRgb(const String& id, uint8_t r, uint8_t g, uint8_t b) {
    const int index = findIndexById(id);
    if (index < 0) return false;
    return applyRgb(static_cast<size_t>(index), r, g, b);
}

bool OutputManager::beep(const String& id, uint16_t frequency, uint16_t durationMs) {
    const int index = findIndexById(id);
    if (index < 0) return false;
    return applyBuzzer(static_cast<size_t>(index), frequency, durationMs);
}

bool OutputManager::loadConfig() {
    _configs.clear();
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
        LOG_W(TAG, "Duplicate output id '" + base + "' remapped to '" + candidate + "'");
        return candidate;
    };

    JsonDocument doc;
    if (!StorageManager::instance().readJson(OF_OUTPUTS_PATH, doc)) {
        return false;
    }
    if (!doc["outputs"].is<JsonArray>()) {
        return true;
    }

    for (auto item : doc["outputs"].as<JsonArray>()) {
        OutputConfig cfg;
        const String type = item["type"] | String("led");
        if (type == "led") cfg.type = OutputType::Led;
        else if (type == "rgb") cfg.type = OutputType::Rgb;
        else if (type == "ws2812") cfg.type = OutputType::Ws2812;
        else if (type == "relay") cfg.type = OutputType::Relay;
        else if (type == "buzzer") cfg.type = OutputType::Buzzer;
        else continue;

        cfg.pin           = item["pin"] | 0;
        cfg.inverted      = item["inverted"] | false;
        cfg.pwm           = item["pwm"] | false;
        cfg.pwmChannel    = item["pwm_channel"] | 0;
        cfg.pwmFrequency  = item["pwm_frequency"] | 5000;
        cfg.pwmResolution = item["pwm_resolution"] | 8;

        cfg.pinR          = item["pin_r"] | 0;
        cfg.pinG          = item["pin_g"] | 0;
        cfg.pinB          = item["pin_b"] | 0;
        cfg.channelR      = item["channel_r"] | 0;
        cfg.channelG      = item["channel_g"] | 1;
        cfg.channelB      = item["channel_b"] | 2;

        cfg.ledCount      = item["led_count"] | 1;
        cfg.brightness    = item["brightness"] | 255;

        cfg.id = ensureUniqueId(item["id"] | String(""), "out", cfg.pin);
        _configs.push_back(cfg);
    }
    return true;
}

void OutputManager::setupOutputs() {
    for (size_t i = 0; i < _configs.size(); ++i) {
        setupOutput(i);
    }
}

void OutputManager::setupOutput(size_t index) {
    const auto& cfg = _configs[index];
    switch (cfg.type) {
        case OutputType::Led:
        case OutputType::Relay:
            if (cfg.pwm) {
                of_pwm_setup(cfg.pin, cfg.pwmChannel, cfg.pwmFrequency, cfg.pwmResolution);
                of_pwm_write(cfg.pin, cfg.pwmChannel, 0);
            } else {
                pinMode(cfg.pin, OUTPUT);
                digitalWrite(cfg.pin, cfg.inverted ? HIGH : LOW);
            }
            break;
        case OutputType::Rgb:
            of_pwm_setup(cfg.pinR, cfg.channelR, cfg.pwmFrequency, cfg.pwmResolution);
            of_pwm_setup(cfg.pinG, cfg.channelG, cfg.pwmFrequency, cfg.pwmResolution);
            of_pwm_setup(cfg.pinB, cfg.channelB, cfg.pwmFrequency, cfg.pwmResolution);
            of_pwm_write(cfg.pinR, cfg.channelR, 0);
            of_pwm_write(cfg.pinG, cfg.channelG, 0);
            of_pwm_write(cfg.pinB, cfg.channelB, 0);
            break;
        case OutputType::Ws2812:
            initWs2812(index);
            break;
        case OutputType::Buzzer:
            of_tone_setup(cfg.pin, cfg.pwmChannel, cfg.pwmFrequency, cfg.pwmResolution);
            of_tone_write(cfg.pin, cfg.pwmChannel, 0);
            break;
    }
}

bool OutputManager::applyDigital(size_t index, bool on) {
    const auto& cfg = _configs[index];
    auto& state = _states[index];
    if (cfg.type != OutputType::Led && cfg.type != OutputType::Relay) return false;

    state.on = on;
    if (cfg.pwm) {
        const uint8_t brightness = on ? 255 : 0;
        const uint32_t maxDuty = (1u << cfg.pwmResolution) - 1u;
        const uint32_t duty = (static_cast<uint32_t>(brightness) * maxDuty) / 255u;
        of_pwm_write(cfg.pin, cfg.pwmChannel, cfg.inverted ? (maxDuty - duty) : duty);
        state.brightness = brightness;
    } else {
        const bool level = cfg.inverted ? !on : on;
        digitalWrite(cfg.pin, level ? HIGH : LOW);
        state.brightness = on ? 255 : 0;
    }
    emitOutputEvent(index, "SetDigital");
    return true;
}

bool OutputManager::applyBrightness(size_t index, uint8_t brightness) {
    const auto& cfg = _configs[index];
    auto& state = _states[index];
    if (cfg.type != OutputType::Led || !cfg.pwm) return false;

    const uint32_t maxDuty = (1u << cfg.pwmResolution) - 1u;
    uint32_t duty = (static_cast<uint32_t>(brightness) * maxDuty) / 255u;
    if (cfg.inverted) duty = maxDuty - duty;
    of_pwm_write(cfg.pin, cfg.pwmChannel, duty);

    state.brightness = brightness;
    state.on = brightness > 0;
    emitOutputEvent(index, "SetBrightness");
    return true;
}

bool OutputManager::applyRgb(size_t index, uint8_t r, uint8_t g, uint8_t b) {
    const auto& cfg = _configs[index];
    auto& state = _states[index];
    if (cfg.type != OutputType::Rgb && cfg.type != OutputType::Ws2812) return false;

    state.red = r;
    state.green = g;
    state.blue = b;
    state.on = (r > 0 || g > 0 || b > 0);

    if (cfg.type == OutputType::Rgb) {
        const uint32_t maxDuty = (1u << cfg.pwmResolution) - 1u;
        auto toDuty = [&](uint8_t v) -> uint32_t {
            uint32_t duty = (static_cast<uint32_t>(v) * maxDuty) / 255u;
            return cfg.inverted ? (maxDuty - duty) : duty;
        };
        of_pwm_write(cfg.pinR, cfg.channelR, toDuty(r));
        of_pwm_write(cfg.pinG, cfg.channelG, toDuty(g));
        of_pwm_write(cfg.pinB, cfg.channelB, toDuty(b));
    } else {
        updateWs2812(index);
    }

    emitOutputEvent(index, "SetRgb");
    return true;
}

bool OutputManager::applyBuzzer(size_t index, uint16_t frequency, uint16_t durationMs) {
    const auto& cfg = _configs[index];
    auto& state = _states[index];
    if (cfg.type != OutputType::Buzzer) return false;

    of_tone_write(cfg.pin, cfg.pwmChannel, frequency);
    state.buzzerFreq = frequency;
    state.on = frequency > 0;
    state.buzzerUntil = durationMs > 0 ? (millis() + durationMs) : 0;
    emitOutputEvent(index, "Beep");
    return true;
}

int OutputManager::findIndexById(const String& id) const {
    for (size_t i = 0; i < _configs.size(); ++i) {
        if (_configs[i].id == id) return static_cast<int>(i);
    }
    return -1;
}

bool OutputManager::initWs2812(size_t index) {
    const auto& cfg = _configs[index];
    auto& state = _states[index];
    if (cfg.ledCount == 0) return false;

    auto& leds = _wsLedsPerOutput[index];
    leds.assign(cfg.ledCount, CRGB::Black);

    bool attached = false;
    // FastLED's clockless WS2812 driver takes the data pin as a compile-time
    // template parameter, so runtime pin selection is done via this switch. Each
    // case instantiates FastPin<PIN>, which static-asserts if the pin is invalid
    // on the target chip — so the candidate list must be per-chip. The pin sets
    // below mirror FastLED's own validity masks (fastpin_esp32.h / _esp8266.h).
#if defined(CONFIG_IDF_TARGET_ESP32S3)
    // ESP32-S3: FastLED masks GPIO 22-25 and 27-32 (SPI flash / invalid).
    #define OF_WS2812_PIN_LIST(X) \
        X(2) X(4) X(5) X(12) X(13) X(14) X(15) X(16) X(17) X(18) X(19) X(21) \
        X(26) X(33) X(38) X(47) X(48)
#elif defined(ESP8266)
    // ESP8266 (default GPIO pin order): GPIO 6-11 are tied to flash.
    #define OF_WS2812_PIN_LIST(X) \
        X(0) X(2) X(4) X(5) X(12) X(13) X(14) X(15) X(16)
#else
    // Classic ESP32: FastLED masks GPIO 6-10 and 20; 34-39 are input-only.
    #define OF_WS2812_PIN_LIST(X) \
        X(2) X(4) X(5) X(12) X(13) X(14) X(15) X(18) X(19) X(21) X(22) X(23) \
        X(25) X(26) X(27) X(32) X(33)
#endif

    #define OF_WS2812_CASE(PIN) \
        case PIN: FastLED.addLeds<WS2812B, PIN, GRB>(leds.data(), cfg.ledCount); attached = true; break;

    switch (cfg.pin) {
        OF_WS2812_PIN_LIST(OF_WS2812_CASE)
        default:
            LOG_W(TAG, "WS2812 pin " + String(cfg.pin) + " not supported on this board");
            break;
    }

    #undef OF_WS2812_CASE
    #undef OF_WS2812_PIN_LIST

    if (!attached) return false;
    FastLED.show();
    state.wsReady = true;
    return true;
}

void OutputManager::updateWs2812(size_t index) {
    const auto& cfg = _configs[index];
    const auto& state = _states[index];
    auto& leds = _wsLedsPerOutput[index];
    if (cfg.type != OutputType::Ws2812 || !state.wsReady || leds.empty()) return;

    auto scale = [&](uint8_t value) -> uint8_t {
        return static_cast<uint8_t>((static_cast<uint16_t>(value) * static_cast<uint16_t>(cfg.brightness)) / 255u);
    };
    const CRGB color(scale(state.red), scale(state.green), scale(state.blue));
    for (auto& led : leds) {
        led = color;
    }
    FastLED.show();
}

void OutputManager::emitOutputEvent(size_t index, const char* action) {
    const auto& cfg = _configs[index];
    const auto& state = _states[index];

    JsonDocument doc;
    doc["action"] = action;
    doc["id"] = cfg.id;
    doc["type"] = static_cast<uint8_t>(cfg.type);
    doc["on"] = state.on;
    doc["brightness"] = state.brightness;
    doc["r"] = state.red;
    doc["g"] = state.green;
    doc["b"] = state.blue;
    doc["buzzer_freq"] = state.buzzerFreq;

    String payload;
    serializeJson(doc, payload);
    EventBus::instance().publish(EventType::OutputStateChanged, cfg.id, payload);
}
