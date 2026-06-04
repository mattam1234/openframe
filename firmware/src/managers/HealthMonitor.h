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

private:
    HealthMonitor() = default;

    uint32_t _loopStartMs      = 0;
    uint32_t _windowStartMs    = 0;
    uint32_t _windowBusyMs     = 0;
    float    _cpuLoadPercent   = 0.0f;
    String   _rebootReason;

    static constexpr uint32_t WINDOW_MS = 5000;
    static constexpr const char* TAG    = "Health";
};
