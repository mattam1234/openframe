#pragma once

#include <Arduino.h>

// Opt-in power management for battery-powered nodes (#7).
//
// Default off — no behaviour change. When `power.mode` is "deep" or "light", the
// node runs normally for `awakeSeconds` after boot, then sleeps for
// `sleepSeconds` (timer wake):
//   - deep:  the SoC powers down and reboots on wake (classic sample→report→sleep
//            duty cycle). ESP8266 supports this only, and needs GPIO16 wired to
//            RST for the timer to wake it.
//   - light: RAM/peripherals are retained; the loop resumes after sleeping and a
//            fresh awake window begins (ESP32 only).
// On ESP32 an optional RTC-capable `power.wake_pin` also wakes the node.
//
// Sleep is suppressed in safe mode so a crash-looping device stays reachable.
class PowerManager {
public:
    static PowerManager& instance();

    void begin();
    void loop();

private:
    PowerManager() = default;

    void enterSleep();

    bool     _enabled       = false;
    bool     _light         = false;   // light vs deep
    uint32_t _awakeMs       = 30000;
    uint64_t _sleepUs       = 300000000ULL;
    int8_t   _wakePin       = -1;
    uint8_t  _wakeLevel     = 1;
    uint32_t _cycleStartMs  = 0;

    static constexpr const char* TAG = "PowerMgr";
};
