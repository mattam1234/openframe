#pragma once

#include <Arduino.h>
#include "../core/Logger.h"

class HealthMonitor {
public:
    static HealthMonitor& instance();

    void begin();

    // Call at the very start of loop() to mark the beginning of a work slice.
    void markLoopStart();

    // Call immediately before delay() at the end of loop() to mark end of work.
    void markLoopEnd();

    // Returns estimated CPU load 0–100% based on loop timing over the last window.
    float getCpuLoadPercent() const { return _cpuLoadPercent; }

    // Human-readable reboot reason (e.g. "Power-on", "Software reset", "Watchdog").
    String getRebootReason() const { return _rebootReason; }

    // True when this boot followed too many rapid unstable boots — the caller
    // should bring up only networking + API/OTA and skip hardware & automation
    // so the user can recover (fix config, OTA, or reboot) over the network.
    bool inSafeMode() const { return _safeMode; }

    // Consecutive unstable-boot count that triggered this boot's mode decision.
    uint32_t getBootCount() const { return _bootCount; }

private:
    HealthMonitor() = default;

    uint32_t _loopStartMs      = 0;
    uint32_t _windowStartMs    = 0;
    uint32_t _windowBusyMs     = 0;
    float    _cpuLoadPercent   = 0.0f;
    String   _rebootReason;

    bool     _safeMode         = false;
    uint32_t _bootCount        = 0;
    bool     _stableMarked     = false;

    static constexpr uint32_t WINDOW_MS = 5000;
    // Consecutive unstable boots before we fall back to safe mode.
    static constexpr uint32_t SAFE_MODE_THRESHOLD = 4;
    // Uptime after which a boot is deemed stable and the crash counter is cleared.
    static constexpr uint32_t STABLE_UPTIME_MS = 30000;
    // Task-watchdog timeout — generous enough to clear a slow setup() (WiFi
    // connect, sensor probing) yet still catch a genuinely wedged loop.
    static constexpr uint32_t WDT_TIMEOUT_SEC = 60;
    static constexpr const char* TAG    = "Health";
};
