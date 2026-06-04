#include "HealthMonitor.h"

#include "../core/PlatformCompat.h"

HealthMonitor& HealthMonitor::instance() {
    static HealthMonitor inst;
    return inst;
}

void HealthMonitor::begin() {
    _windowStartMs  = millis();
    _windowBusyMs   = 0;
    _cpuLoadPercent = 0.0f;

    // Determine reboot reason once at startup (platform-abstracted)
    _rebootReason = of_reboot_reason();

    LOG_I(TAG, "Initialised — reboot reason: " + _rebootReason);
}

void HealthMonitor::markLoopStart() {
    _loopStartMs = millis();

    // Flush the measurement window every WINDOW_MS
    if (_windowStartMs == 0) {
        _windowStartMs = _loopStartMs;
    }

    const uint32_t windowElapsed = _loopStartMs - _windowStartMs;
    if (windowElapsed >= WINDOW_MS) {
        if (windowElapsed > 0) {
            _cpuLoadPercent = static_cast<float>(_windowBusyMs) / static_cast<float>(windowElapsed) * 100.0f;
            if (_cpuLoadPercent > 100.0f) _cpuLoadPercent = 100.0f;
        }
        _windowStartMs = _loopStartMs;
        _windowBusyMs  = 0;
    }
}

void HealthMonitor::markLoopEnd() {
    const uint32_t nowMs = millis();
    if (_loopStartMs > 0 && nowMs >= _loopStartMs) {
        _windowBusyMs += (nowMs - _loopStartMs);
    }
}
