#include "OutputManager.h"
#include <map>
#include <math.h>
#include "../core/PlatformCompat.h"

namespace {
// Map an 8-bit level (0–255) to a PWM duty in [0, maxDuty]. With gamma on, applies
// γ≈2.8 so perceived brightness ramps linearly (the eye is very non-linear). The
// 0 and 255 endpoints are exact either way. Inversion is left to the caller.
uint32_t pwmDuty(uint8_t value, uint32_t maxDuty, bool gamma) {
    if (gamma && value > 0 && value < 255) {
        const float norm = powf(static_cast<float>(value) / 255.0f, 2.8f);
        return static_cast<uint32_t>(norm * maxDuty + 0.5f);
    }
    return (static_cast<uint32_t>(value) * maxDuty) / 255u;
}
}  // namespace

const char* ledAnimationToString(LedAnimation a) {
    switch (a) {
        case LedAnimation::Solid:     return "solid";
        case LedAnimation::Off:       return "off";
        case LedAnimation::Blink:     return "blink";
        case LedAnimation::Breathe:   return "breathe";
        case LedAnimation::Rainbow:   return "rainbow";
        case LedAnimation::Chase:     return "chase";
        case LedAnimation::ColorWipe: return "colorwipe";
        case LedAnimation::Fire:      return "fire";
    }
    return "solid";
}

LedAnimation ledAnimationFromString(const String& s) {
    if (s == "off")       return LedAnimation::Off;
    if (s == "blink")     return LedAnimation::Blink;
    if (s == "breathe")   return LedAnimation::Breathe;
    if (s == "rainbow")   return LedAnimation::Rainbow;
    if (s == "chase")     return LedAnimation::Chase;
    if (s == "colorwipe") return LedAnimation::ColorWipe;
    if (s == "fire")      return LedAnimation::Fire;
    return LedAnimation::Solid;
}

const char* OutputManager::outputTypeToString(OutputType type) {
    switch (type) {
        case OutputType::Led:    return "led";
        case OutputType::Rgb:    return "rgb";
        case OutputType::Ws2812: return "ws2812";
        case OutputType::Relay:  return "relay";
        case OutputType::Buzzer: return "buzzer";
        case OutputType::Servo:  return "servo";
        case OutputType::Stepper: return "stepper";
    }
    return "led";
}

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
    tickAnimations(nowMs);
    tickSteppers(micros());
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

bool OutputManager::setAngle(const String& id, uint8_t angle) {
    const int index = findIndexById(id);
    if (index < 0) return false;
    return applyServo(static_cast<size_t>(index), angle);
}

bool OutputManager::setPosition(const String& id, int32_t steps) {
    const int index = findIndexById(id);
    if (index < 0) return false;
    if (_configs[index].type != OutputType::Stepper) return false;
    _states[index].stepTarget = steps;
    emitOutputEvent(static_cast<size_t>(index), "SetPosition");
    return true;
}

void OutputManager::tickSteppers(uint32_t nowUs) {
    for (size_t i = 0; i < _configs.size(); ++i) {
        const auto& cfg = _configs[i];
        if (cfg.type != OutputType::Stepper) continue;
        auto& st = _states[i];
        if (st.stepPos == st.stepTarget) continue;

        const uint32_t intervalUs = 1000000UL / (cfg.maxStepHz ? cfg.maxStepHz : 1000);
        if (st.lastStepUs != 0 && (nowUs - st.lastStepUs) < intervalUs) continue;
        st.lastStepUs = nowUs;

        const bool forward = st.stepTarget > st.stepPos;
        digitalWrite(cfg.pinDir, (forward != cfg.inverted) ? HIGH : LOW);
        // One step pulse (driver latches on the rising edge; a few µs HIGH is plenty).
        digitalWrite(cfg.pin, HIGH);
        delayMicroseconds(3);
        digitalWrite(cfg.pin, LOW);
        st.stepPos += forward ? 1 : -1;
        if (st.stepPos == st.stepTarget) emitOutputEvent(i, "PositionReached");
    }
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
        else if (type == "servo") cfg.type = OutputType::Servo;
        else if (type == "stepper") cfg.type = OutputType::Stepper;
        else continue;

        cfg.pin           = item["pin"] | 0;
        cfg.inverted      = item["inverted"] | false;
        cfg.pwm           = item["pwm"] | false;
        cfg.pwmChannel    = item["pwm_channel"] | 0;
        cfg.pwmFrequency  = item["pwm_frequency"] | 5000;
        cfg.pwmResolution = item["pwm_resolution"] | 8;
        cfg.gamma         = item["gamma"] | false;

        if (cfg.type == OutputType::Servo) {
            // Servos are driven at 50 Hz; 16-bit duty gives smooth positioning on
            // ESP32. (ESP8266 analogWriteFreq is global — a servo there forces all
            // PWM outputs to 50 Hz, so pair servos with non-PWM outputs.)
            cfg.pwmFrequency  = item["pwm_frequency"]  | 50;
            cfg.pwmResolution = item["pwm_resolution"] | 16;
            cfg.servoMinUs    = item["servo_min_us"]   | 500;
            cfg.servoMaxUs    = item["servo_max_us"]   | 2500;
        }
        if (cfg.type == OutputType::Stepper) {
            cfg.pinDir      = item["pin_dir"]       | 0;
            cfg.pinEnable   = item["pin_enable"]    | 0;
            cfg.stepsPerRev = item["steps_per_rev"] | 200;
            cfg.maxStepHz   = item["max_step_hz"]   | 1000;
        }

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
        case OutputType::Servo:
            of_pwm_setup(cfg.pin, cfg.pwmChannel, cfg.pwmFrequency, cfg.pwmResolution);
            applyServo(index, 90);  // park at centre
            break;
        case OutputType::Stepper:
            pinMode(cfg.pin, OUTPUT);          // STEP
            pinMode(cfg.pinDir, OUTPUT);       // DIR
            digitalWrite(cfg.pin, LOW);
            digitalWrite(cfg.pinDir, LOW);
            if (cfg.pinEnable) {               // active-LOW enable
                pinMode(cfg.pinEnable, OUTPUT);
                digitalWrite(cfg.pinEnable, LOW);
            }
            break;
    }
}

bool OutputManager::applyDigital(size_t index, bool on) {
    const auto& cfg = _configs[index];
    auto& state = _states[index];

    if (cfg.type == OutputType::Ws2812) {
        state.on = on;
        renderWs2812(index);
        emitOutputEvent(index, "SetDigital");
        return true;
    }

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

    if (cfg.type == OutputType::Ws2812) {
        state.brightness = brightness;
        state.on = brightness > 0;
        renderWs2812(index);
        emitOutputEvent(index, "SetBrightness");
        return true;
    }

    if (cfg.type != OutputType::Led || !cfg.pwm) return false;

    const uint32_t maxDuty = (1u << cfg.pwmResolution) - 1u;
    uint32_t duty = pwmDuty(brightness, maxDuty, cfg.gamma);
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
            uint32_t duty = pwmDuty(v, maxDuty, cfg.gamma);
            return cfg.inverted ? (maxDuty - duty) : duty;
        };
        of_pwm_write(cfg.pinR, cfg.channelR, toDuty(r));
        of_pwm_write(cfg.pinG, cfg.channelG, toDuty(g));
        of_pwm_write(cfg.pinB, cfg.channelB, toDuty(b));
    } else {
        // Setting a colour implies a solid fill; clear any running animation.
        state.animation = LedAnimation::Solid;
        renderWs2812(index);
    }

    emitOutputEvent(index, "SetRgb");
    return true;
}

bool OutputManager::setAnimation(const String& id, LedAnimation animation, uint8_t speed) {
    const int index = findIndexById(id);
    if (index < 0) return false;
    const auto& cfg = _configs[index];
    auto& state = _states[static_cast<size_t>(index)];
    if (cfg.type != OutputType::Ws2812) return false;

    state.animation = animation;
    if (speed > 0) state.animationSpeed = speed;
    state.on = (animation != LedAnimation::Off);
    state.animPhase = 0;
    state.animLastMs = 0;
    renderWs2812(static_cast<size_t>(index));
    emitOutputEvent(static_cast<size_t>(index), "SetAnimation");
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

bool OutputManager::applyServo(size_t index, uint8_t angle) {
    const auto& cfg = _configs[index];
    auto& state = _states[index];
    if (cfg.type != OutputType::Servo) return false;

    if (angle > 180) angle = 180;

    // angle → pulse width (µs) → duty fraction of the 50 Hz (20 ms) period.
    const uint32_t pulseUs = cfg.servoMinUs +
        (static_cast<uint32_t>(cfg.servoMaxUs - cfg.servoMinUs) * angle) / 180u;
    const uint32_t periodUs = 1000000u / (cfg.pwmFrequency ? cfg.pwmFrequency : 50u);
    const uint32_t maxDuty  = (1u << cfg.pwmResolution) - 1u;
    uint32_t duty = (pulseUs * maxDuty) / periodUs;
    if (cfg.inverted) duty = maxDuty - duty;

    of_pwm_write(cfg.pin, cfg.pwmChannel, duty);
    state.angle = angle;
    state.on    = true;
    emitOutputEvent(index, "SetAngle");
    return true;
}

void OutputManager::identify() {
    // Remember the on-state of simple digital outputs so we can restore them
    // after the attention-grabbing blink pattern.
    std::vector<std::pair<size_t, bool>> savedDigital;
    for (size_t i = 0; i < _configs.size(); ++i) {
        const OutputType t = _configs[i].type;
        if (t == OutputType::Led || t == OutputType::Relay || t == OutputType::Ws2812) {
            savedDigital.emplace_back(i, _states[i].on);
        }
    }

    constexpr int CYCLES = 5;
    for (int c = 0; c < CYCLES; ++c) {
        for (const auto& d : savedDigital) applyDigital(d.first, true);
        for (size_t i = 0; i < _configs.size(); ++i) {
            if (_configs[i].type == OutputType::Buzzer) applyBuzzer(i, 2200, 90);
        }
        delay(120);
        for (const auto& d : savedDigital) applyDigital(d.first, false);
        delay(120);
    }

    // Restore the outputs we toggled.
    for (const auto& d : savedDigital) applyDigital(d.first, d.second);
    LOG_I(TAG, "Identify pulse complete");
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
#elif defined(CONFIG_IDF_TARGET_ESP32C3)
    // ESP32-C3: 22 GPIOs; FastLED masks GPIO 11-17 (SPI flash). GPIO 18-19 are
    // the native USB pins and 20-21 the default UART, so usable but reserved.
    #define OF_WS2812_PIN_LIST(X) \
        X(0) X(1) X(2) X(3) X(4) X(5) X(6) X(7) X(8) X(9) X(10) \
        X(18) X(19) X(20) X(21)
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

    // Seed runtime state: strip starts off, at its configured master brightness.
    state.brightness    = cfg.brightness;
    state.animation     = LedAnimation::Solid;
    state.on            = false;
    state.animPhase     = 0;
    state.animLastMs    = 0;
    state.wsReady       = true;

    FastLED.show();
    return true;
}

// Render the current frame for one strip and push it to the hardware. Honours
// state.on, the active animation, the current colour, the animation phase and
// the per-strip master brightness.
void OutputManager::renderWs2812(size_t index) {
    const auto& cfg = _configs[index];
    const auto& state = _states[index];
    auto& leds = _wsLedsPerOutput[index];
    if (cfg.type != OutputType::Ws2812 || !state.wsReady || leds.empty()) return;

    const uint8_t  master = state.brightness;
    const uint16_t n      = static_cast<uint16_t>(leds.size());
    const CRGB     base(state.red, state.green, state.blue);

    auto fillBlack = [&]() { for (auto& l : leds) l = CRGB::Black; };

    if (!state.on || state.animation == LedAnimation::Off) {
        fillBlack();
        FastLED.show();
        return;
    }

    switch (state.animation) {
        case LedAnimation::Solid: {
            CRGB c = base; c.nscale8_video(master);
            for (auto& l : leds) l = c;
            break;
        }
        case LedAnimation::Blink: {
            const bool lit = (state.animPhase & 1) == 0;
            CRGB c = base; c.nscale8_video(lit ? master : 0);
            for (auto& l : leds) l = c;
            break;
        }
        case LedAnimation::Breathe: {
            const uint8_t lvl = scale8(triwave8(static_cast<uint8_t>(state.animPhase)), master);
            CRGB c = base; c.nscale8_video(lvl);
            for (auto& l : leds) l = c;
            break;
        }
        case LedAnimation::Rainbow: {
            const uint8_t startHue = static_cast<uint8_t>(state.animPhase);
            const uint8_t delta    = n > 0 ? (n >= 255 ? 1 : static_cast<uint8_t>(255 / n)) : 0;
            fill_rainbow(leds.data(), n, startHue, delta ? delta : 1);
            for (auto& l : leds) l.nscale8_video(master);
            break;
        }
        case LedAnimation::Chase: {
            fillBlack();
            if (n > 0) {
                const uint16_t pos = state.animPhase % n;
                CRGB c = base; c.nscale8_video(master);
                leds[pos] = c;
                if (n > 1) { CRGB t = c; t.nscale8(80); leds[(pos + n - 1) % n] = t; }  // short tail
            }
            break;
        }
        case LedAnimation::ColorWipe: {
            const uint16_t count = n > 0 ? (state.animPhase % (n + 1)) : 0;
            CRGB c = base; c.nscale8_video(master);
            for (uint16_t i = 0; i < n; ++i) leds[i] = (i < count) ? c : CRGB::Black;
            break;
        }
        case LedAnimation::Fire: {
            // Stateless flame: Perlin noise over position + time drives a heat
            // palette. Ignores the set colour (fire is its own palette).
            for (uint16_t i = 0; i < n; ++i) {
                const uint8_t heat = inoise8(i * 60, state.animPhase * 40);
                CRGB c = HeatColor(heat);
                c.nscale8_video(master);
                leds[i] = c;
            }
            break;
        }
        case LedAnimation::Off:
            break;  // handled above
    }
    FastLED.show();
}

void OutputManager::tickAnimations(uint32_t nowMs) {
    for (size_t i = 0; i < _configs.size(); ++i) {
        const auto& cfg = _configs[i];
        auto& state = _states[i];
        if (cfg.type != OutputType::Ws2812 || !state.wsReady || !state.on) continue;
        // Static animations need no per-frame advance.
        if (state.animation == LedAnimation::Solid || state.animation == LedAnimation::Off) continue;

        // Faster speed → shorter frame interval (≈10ms fast … 100ms slow).
        const uint32_t interval = 10u + ((255u - state.animationSpeed) * 90u) / 255u;
        if (state.animLastMs != 0 && (nowMs - state.animLastMs) < interval) continue;
        state.animLastMs = nowMs;
        state.animPhase++;
        renderWs2812(i);
    }
}

void OutputManager::fillStateJson(JsonArray& arr) const {
    for (size_t i = 0; i < _configs.size(); ++i) {
        const auto& cfg = _configs[i];
        const auto& state = _states[i];
        auto obj = arr.add<JsonObject>();
        obj["id"]   = cfg.id;
        obj["type"] = outputTypeToString(cfg.type);
        obj["on"]   = state.on;
        obj["brightness"] = state.brightness;
        obj["r"] = state.red;
        obj["g"] = state.green;
        obj["b"] = state.blue;
        if (cfg.type == OutputType::Led || cfg.type == OutputType::Rgb) obj["gamma"] = cfg.gamma;
        if (cfg.type == OutputType::Led) obj["pwm"] = cfg.pwm;
        if (cfg.type == OutputType::Ws2812) {
            obj["led_count"]  = cfg.ledCount;
            obj["animation"]  = ledAnimationToString(state.animation);
            obj["speed"]      = state.animationSpeed;
        }
        if (cfg.type == OutputType::Buzzer) {
            obj["buzzer_freq"] = state.buzzerFreq;
        }
        if (cfg.type == OutputType::Stepper) {
            obj["position"]      = state.stepPos;
            obj["target"]        = state.stepTarget;
            obj["steps_per_rev"] = cfg.stepsPerRev;
        }
        if (cfg.type == OutputType::Servo) {
            obj["angle"]         = state.angle;
            obj["servo_min_us"]  = cfg.servoMinUs;
            obj["servo_max_us"]  = cfg.servoMaxUs;
        }
    }
}

void OutputManager::applyStateJson(JsonArrayConst arr) {
    for (JsonObjectConst obj : arr) {
        const String id = obj["id"] | String("");
        if (!id.length()) continue;
        const int index = findIndexById(id);
        if (index < 0) continue;
        const OutputType type = _configs[index].type;

        // Apply type-specific attributes first, then the on/off state last so the
        // final digital level reflects the restored colour/brightness.
        if (type == OutputType::Rgb || type == OutputType::Ws2812) {
            setRgb(id, obj["r"] | 0, obj["g"] | 0, obj["b"] | 0);
        }
        if (type == OutputType::Ws2812) {
            setAnimation(id, ledAnimationFromString(obj["animation"] | String("solid")),
                         obj["speed"] | 128);
        }
        if (obj["brightness"].is<int>()) setBrightness(id, obj["brightness"].as<int>());
        if (type == OutputType::Servo && obj["angle"].is<int>()) {
            setAngle(id, static_cast<uint8_t>(obj["angle"].as<int>()));
        }
        if (type == OutputType::Stepper && obj["position"].is<int>()) {
            setPosition(id, obj["position"].as<int32_t>());
        }
        if (obj["on"].is<bool>()) setDigital(id, obj["on"].as<bool>());
    }
}

void OutputManager::emitOutputEvent(size_t index, const char* action) {
    const auto& cfg = _configs[index];
    const auto& state = _states[index];

    JsonDocument doc;
    doc["action"] = action;
    doc["id"] = cfg.id;
    doc["type"] = outputTypeToString(cfg.type);
    doc["on"] = state.on;
    doc["brightness"] = state.brightness;
    doc["r"] = state.red;
    doc["g"] = state.green;
    doc["b"] = state.blue;
    doc["buzzer_freq"] = state.buzzerFreq;
    if (cfg.type == OutputType::Ws2812) {
        doc["animation"] = ledAnimationToString(state.animation);
        doc["speed"] = state.animationSpeed;
    }
    if (cfg.type == OutputType::Servo) {
        doc["angle"] = state.angle;
    }

    String payload;
    serializeJson(doc, payload);
    EventBus::instance().publish(EventType::OutputStateChanged, cfg.id, payload);
}
