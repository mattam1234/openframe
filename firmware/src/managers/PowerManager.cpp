#include "PowerManager.h"

#include "../core/ConfigManager.h"
#include "../core/Logger.h"
#include "HealthMonitor.h"

#if defined(ESP32)
    #include <esp_sleep.h>
#endif

PowerManager& PowerManager::instance() {
    static PowerManager inst;
    return inst;
}

void PowerManager::begin() {
    const auto& p = ConfigManager::instance().config().power;
    String mode = p.mode;
    mode.toLowerCase();

    if (mode != "deep" && mode != "light") {
        LOG_I(TAG, "Power management off");
        return;
    }

#if defined(ESP8266)
    if (mode == "light") {
        // ESP8266 has no equivalent blocking light-sleep call here; fall back to
        // staying awake rather than silently behaving like deep sleep.
        LOG_W(TAG, "Light sleep unsupported on ESP8266 — power management disabled");
        return;
    }
#endif

    _enabled      = true;
    _light        = (mode == "light");
    _awakeMs      = (p.awakeSeconds ? p.awakeSeconds : 1u) * 1000UL;
    _sleepUs      = (uint64_t)(p.sleepSeconds ? p.sleepSeconds : 1u) * 1000000ULL;
    _wakePin      = p.wakePin;
    _wakeLevel    = p.wakeLevel ? 1 : 0;
    _cycleStartMs = millis();

    LOG_I(TAG, String(_light ? "Light" : "Deep") + " sleep: awake " +
                   String(p.awakeSeconds) + "s, then sleep " + String(p.sleepSeconds) + "s" +
                   (_wakePin >= 0 ? (" (or wake on GPIO" + String(_wakePin) + ")") : ""));
}

void PowerManager::loop() {
    if (!_enabled) return;
    // Don't strand a misbehaving device: stay awake in safe mode.
    if (HealthMonitor::instance().inSafeMode()) return;
    if ((uint32_t)(millis() - _cycleStartMs) < _awakeMs) return;
    enterSleep();
}

void PowerManager::enterSleep() {
    LOG_W(TAG, String("Entering ") + (_light ? "light" : "deep") + " sleep");
    delay(50);  // let the log + any in-flight TX flush

#if defined(ESP32)
    esp_sleep_enable_timer_wakeup(_sleepUs);
    // ext0 GPIO wake needs an RTC-capable pin and isn't available on every ESP32
    // variant — guard on the SoC capability so all targets compile.
  #if defined(SOC_PM_SUPPORT_EXT0_WAKEUP) && SOC_PM_SUPPORT_EXT0_WAKEUP
    if (_wakePin >= 0) {
        esp_sleep_enable_ext0_wakeup(static_cast<gpio_num_t>(_wakePin), _wakeLevel);
    }
  #endif
    if (_light) {
        esp_light_sleep_start();
        // Resumed — begin a fresh awake window for the next cycle.
        _cycleStartMs = millis();
        LOG_I(TAG, "Woke from light sleep");
    } else {
        esp_deep_sleep_start();  // never returns — reboots on wake
    }
#elif defined(ESP8266)
    // Timer-only deep sleep; requires GPIO16 wired to RST. Never returns.
    ESP.deepSleep(_sleepUs);
#endif
}
